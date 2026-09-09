#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

struct request {
    pthread_mutex_t lock;
    sem_t ready;
    int error;
    int completion_expected;
    int processor_using;
    int processor_done;
    int freed;
    int req_count;
};

static void *processor(void *arg)
{
    struct request *r = arg;

    pthread_mutex_lock(&r->lock);
    r->processor_using = 1;
    pthread_mutex_unlock(&r->lock);

    /* Deterministically leave a window where an early free would be unsafe. */
    usleep(20000);

    pthread_mutex_lock(&r->lock);
    assert(!r->freed);
    r->processor_using = 0;
    r->processor_done = 1;
    pthread_mutex_unlock(&r->lock);
    assert(sem_post(&r->ready) == 0);
    return NULL;
}

static int wait_and_free(struct request *r)
{
    int err;
    int completion_expected;

    pthread_mutex_lock(&r->lock);
    err = r->error;
    completion_expected = r->completion_expected;
    pthread_mutex_unlock(&r->lock);

    if (completion_expected) {
        while (sem_wait(&r->ready) == -1)
            assert(errno == EINTR);
    } else if (err) {
        goto out;
    }

    pthread_mutex_lock(&r->lock);
    err = r->error;
    pthread_mutex_unlock(&r->lock);

out:
    pthread_mutex_lock(&r->lock);
    assert(!r->processor_using);
    assert(!r->freed);
    r->freed = 1;
    r->req_count--;
    pthread_mutex_unlock(&r->lock);
    return err;
}

static void init_request(struct request *r)
{
    *r = (struct request){0};
    assert(pthread_mutex_init(&r->lock, NULL) == 0);
    assert(sem_init(&r->ready, 0, 0) == 0);
    r->req_count = 1;
}

static void destroy_request(struct request *r)
{
    assert(r->freed);
    assert(r->req_count == 0);
    assert(sem_destroy(&r->ready) == 0);
    assert(pthread_mutex_destroy(&r->lock) == 0);
}

int main(void)
{
    struct request local;
    struct request raced;
    struct request reclaimed;
    pthread_t th;

    /* Pre-publication local failure: nobody will post completion. */
    init_request(&local);
    local.error = -EPIPE;
    local.completion_expected = 0;
    assert(wait_and_free(&local) == -EPIPE);
    destroy_request(&local);

    /*
     * Published request: reply processing already owns the completion handoff.
     * A local send error must not allow the waiter to free before ready is
     * posted by that processing path.
     */
    init_request(&raced);
    raced.completion_expected = 1;
    assert(pthread_create(&th, NULL, processor, &raced) == 0);
    usleep(5000);
    pthread_mutex_lock(&raced.lock);
    assert(raced.processor_using);
    raced.error = -EIO;
    pthread_mutex_unlock(&raced.lock);
    assert(wait_and_free(&raced) == -EIO);
    assert(pthread_join(th, NULL) == 0);
    assert(raced.processor_done);
    destroy_request(&raced);

    /* If sender removes its own reqtab entry, no remote handoff remains. */
    init_request(&reclaimed);
    reclaimed.completion_expected = 1;
    pthread_mutex_lock(&reclaimed.lock);
    reclaimed.completion_expected = 0;
    reclaimed.error = -EIO;
    pthread_mutex_unlock(&reclaimed.lock);
    assert(wait_and_free(&reclaimed) == -EIO);
    destroy_request(&reclaimed);

    puts("request completion handoff: PASS");
    return 0;
}
