#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct chunk {
    pthread_mutex_t lock;
    int refs;
    int freed;
    int callbacks;
};

struct request {
    struct chunk *chunk;
};

static void chunk_put_locked(struct chunk *c)
{
    pthread_mutex_lock(&c->lock);
    assert(c->refs > 0);
    c->refs--;
    if (c->refs == 0)
        c->freed = 1;
    pthread_mutex_unlock(&c->lock);
}

static void read_begin(struct request *r)
{
    struct chunk *c = r->chunk;
    pthread_mutex_lock(&c->lock);
    assert(!c->freed);
    c->refs++; /* request owns callback state */
    pthread_mutex_unlock(&c->lock);
}

static void read_end(struct request *r)
{
    struct chunk *c = r->chunk;
    pthread_mutex_lock(&c->lock);
    assert(!c->freed);
    c->callbacks++;
    assert(c->refs > 0);
    c->refs--; /* return request ownership */
    if (c->refs == 0)
        c->freed = 1;
    pthread_mutex_unlock(&c->lock);
}

static void *complete_request(void *arg)
{
    read_end(arg);
    return NULL;
}

int main(void)
{
    struct chunk c = {0};
    struct request r1 = { .chunk = &c };
    struct request r2 = { .chunk = &c };
    pthread_t t1, t2;

    assert(pthread_mutex_init(&c.lock, NULL) == 0);
    c.refs = 1; /* cache/read-call owner */

    read_begin(&r1);
    read_begin(&r2);
    assert(c.refs == 3);

    /* release()/cache replacement may discard its ownership first. */
    chunk_put_locked(&c);
    assert(c.refs == 2);
    assert(!c.freed);

    assert(pthread_create(&t1, NULL, complete_request, &r1) == 0);
    assert(pthread_create(&t2, NULL, complete_request, &r2) == 0);
    assert(pthread_join(t1, NULL) == 0);
    assert(pthread_join(t2, NULL) == 0);

    assert(c.callbacks == 2);
    assert(c.refs == 0);
    assert(c.freed);

    assert(pthread_mutex_destroy(&c.lock) == 0);
    puts("read request chunk lifetime: PASS");
    return 0;
}
