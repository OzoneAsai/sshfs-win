#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct conn {
    int connver;
};

struct request {
    struct conn *conn;
    int connver;
    size_t len;
    int outstanding_accounted;
};

struct accounting {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    size_t outstanding_len;
    size_t max_outstanding_len;
};

static void accounting_init(struct accounting *a, size_t max)
{
    memset(a, 0, sizeof(*a));
    assert(pthread_mutex_init(&a->lock, NULL) == 0);
    assert(pthread_cond_init(&a->cond, NULL) == 0);
    a->max_outstanding_len = max;
}

static void accounting_destroy(struct accounting *a)
{
    assert(pthread_cond_destroy(&a->cond) == 0);
    assert(pthread_mutex_destroy(&a->lock) == 0);
}

static void release_locked(struct accounting *a, struct request *req)
{
    int was_over;

    if (!req->outstanding_accounted)
        return;
    assert(a->outstanding_len >= req->len);
    was_over = a->outstanding_len > a->max_outstanding_len;
    a->outstanding_len -= req->len;
    req->outstanding_accounted = 0;
    if (was_over || a->outstanding_len < a->max_outstanding_len)
        assert(pthread_cond_broadcast(&a->cond) == 0);
}

static int admit_locked(struct accounting *a, struct request *req)
{
    while (a->outstanding_len != 0 &&
           (a->outstanding_len >= a->max_outstanding_len ||
            req->len > a->max_outstanding_len - a->outstanding_len)) {
        assert(pthread_cond_wait(&a->cond, &a->lock) == 0);
        if (req->connver != req->conn->connver)
            return -EIO;
    }

    assert(!req->outstanding_accounted);
    a->outstanding_len += req->len;
    req->outstanding_accounted = 1;
    return 0;
}

struct waiter_ctx {
    struct accounting *a;
    struct request *req;
    int result;
    atomic_int entered;
};

static void *waiter_main(void *opaque)
{
    struct waiter_ctx *ctx = opaque;
    pthread_mutex_lock(&ctx->a->lock);
    atomic_store_explicit(&ctx->entered, 1, memory_order_release);
    ctx->result = admit_locked(ctx->a, ctx->req);
    pthread_mutex_unlock(&ctx->a->lock);
    return NULL;
}

static void wait_until_entered(atomic_int *entered)
{
    const struct timespec pause = { .tv_sec = 0, .tv_nsec = 1000000L };
    int i;

    for (i = 0; i < 1000; i++) {
        if (atomic_load_explicit(entered, memory_order_acquire))
            return;
        assert(nanosleep(&pause, NULL) == 0);
    }
    assert(atomic_load_explicit(entered, memory_order_acquire));
}

static void test_normal_and_idempotent_release(void)
{
    struct accounting a;
    struct conn c = { .connver = 7 };
    struct request r = { .conn = &c, .connver = 7, .len = 40 };

    accounting_init(&a, 100);
    pthread_mutex_lock(&a.lock);
    assert(admit_locked(&a, &r) == 0);
    assert(a.outstanding_len == 40);
    release_locked(&a, &r);
    assert(a.outstanding_len == 0);
    release_locked(&a, &r);
    assert(a.outstanding_len == 0);
    pthread_mutex_unlock(&a.lock);
    accounting_destroy(&a);
}

static void test_multi_connection_teardown_preserves_other_bytes(void)
{
    struct accounting a;
    struct conn ca = { .connver = 1 }, cb = { .connver = 2 };
    struct request ra = { .conn = &ca, .connver = 1, .len = 40 };
    struct request rb = { .conn = &cb, .connver = 2, .len = 30 };

    accounting_init(&a, 100);
    pthread_mutex_lock(&a.lock);
    assert(admit_locked(&a, &ra) == 0);
    assert(admit_locked(&a, &rb) == 0);
    assert(a.outstanding_len == 70);
    release_locked(&a, &ra); /* conn A teardown */
    ca.connver++;
    assert(a.outstanding_len == 30);
    release_locked(&a, &rb);
    assert(a.outstanding_len == 0);
    pthread_mutex_unlock(&a.lock);
    accounting_destroy(&a);
}

static void test_waiter_does_not_own_bytes_and_generation_change_fails(void)
{
    struct accounting a;
    struct conn c = { .connver = 3 };
    struct request active = { .conn = &c, .connver = 3, .len = 90 };
    struct request waiting = { .conn = &c, .connver = 3, .len = 20 };
    struct waiter_ctx ctx = { .a = &a, .req = &waiting };
    pthread_t thread;

    accounting_init(&a, 100);
    pthread_mutex_lock(&a.lock);
    assert(admit_locked(&a, &active) == 0);
    pthread_mutex_unlock(&a.lock);

    assert(pthread_create(&thread, NULL, waiter_main, &ctx) == 0);
    wait_until_entered(&ctx.entered);
    { const struct timespec pause = { .tv_sec = 0, .tv_nsec = 5000000L }; assert(nanosleep(&pause, NULL) == 0); }

    pthread_mutex_lock(&a.lock);
    /* The waiting request must not inflate the global byte count. */
    assert(a.outstanding_len == 90);
    release_locked(&a, &active);
    c.connver++;
    assert(pthread_cond_broadcast(&a.cond) == 0);
    pthread_mutex_unlock(&a.lock);

    assert(pthread_join(thread, NULL) == 0);
    assert(ctx.result == -EIO);
    assert(!waiting.outstanding_accounted);
    assert(a.outstanding_len == 0);
    accounting_destroy(&a);
}

static void test_single_oversized_request_makes_progress(void)
{
    struct accounting a;
    struct conn c = { .connver = 11 };
    struct request big = { .conn = &c, .connver = 11, .len = 80 };
    struct request small = { .conn = &c, .connver = 11, .len = 8 };
    struct waiter_ctx ctx = { .a = &a, .req = &small };
    pthread_t thread;

    accounting_init(&a, 64);
    pthread_mutex_lock(&a.lock);
    assert(admit_locked(&a, &big) == 0);
    assert(a.outstanding_len == 80);
    pthread_mutex_unlock(&a.lock);

    assert(pthread_create(&thread, NULL, waiter_main, &ctx) == 0);
    wait_until_entered(&ctx.entered);
    { const struct timespec pause = { .tv_sec = 0, .tv_nsec = 5000000L }; assert(nanosleep(&pause, NULL) == 0); }
    pthread_mutex_lock(&a.lock);
    assert(a.outstanding_len == 80);
    release_locked(&a, &big);
    pthread_mutex_unlock(&a.lock);

    assert(pthread_join(thread, NULL) == 0);
    assert(ctx.result == 0);
    pthread_mutex_lock(&a.lock);
    assert(a.outstanding_len == 8);
    release_locked(&a, &small);
    pthread_mutex_unlock(&a.lock);
    accounting_destroy(&a);
}

int main(void)
{
    int i;
    for (i = 0; i < 100; i++) {
        test_normal_and_idempotent_release();
        test_multi_connection_teardown_preserves_other_bytes();
        test_waiter_does_not_own_bytes_and_generation_change_fails();
        test_single_oversized_request_makes_progress();
    }
    puts("outstanding accounting lifecycle: PASS (100 rounds)");
    return 0;
}
