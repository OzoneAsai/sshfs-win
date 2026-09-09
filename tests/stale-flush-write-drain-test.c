#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

struct file_state {
    pthread_mutex_t lock;
    pthread_cond_t finished;
    pthread_cond_t waiter_started;
    int pending;
    int write_error;
    int stale;
    int waiting;
    int callback_done;
};

static int flush_model(struct file_state *s)
{
    int err;
    int stale;

    pthread_mutex_lock(&s->lock);
    while (s->pending) {
        s->waiting = 1;
        pthread_cond_broadcast(&s->waiter_started);
        pthread_cond_wait(&s->finished, &s->lock);
    }
    err = s->write_error;
    s->write_error = 0;
    stale = s->stale;
    pthread_mutex_unlock(&s->lock);

    if (err)
        return err;
    return stale ? -EIO : 0;
}

static void *flush_thread(void *arg)
{
    struct file_state *s = arg;
    return (void *)(long)flush_model(s);
}

int main(void)
{
    struct file_state s = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .finished = PTHREAD_COND_INITIALIZER,
        .waiter_started = PTHREAD_COND_INITIALIZER,
        .pending = 1,
        .stale = 1,
    };
    pthread_t t;
    void *ret;

    assert(pthread_create(&t, NULL, flush_thread, &s) == 0);
    pthread_mutex_lock(&s.lock);
    while (!s.waiting)
        pthread_cond_wait(&s.waiter_started, &s.lock);
    /* A stale flush must still be blocked while callback ownership exists. */
    assert(s.pending == 1);
    s.callback_done = 1;
    s.pending = 0;
    pthread_cond_broadcast(&s.finished);
    pthread_mutex_unlock(&s.lock);
    assert(pthread_join(t, &ret) == 0);
    assert((long)ret == -EIO);
    assert(s.callback_done == 1);

    /* A concrete write failure outranks the generic stale-generation error. */
    s.pending = 0;
    s.stale = 1;
    s.write_error = -ENOSPC;
    assert(flush_model(&s) == -ENOSPC);

    pthread_cond_destroy(&s.waiter_started);
    pthread_cond_destroy(&s.finished);
    pthread_mutex_destroy(&s.lock);
    puts("stale flush write drain: PASS");
    return 0;
}
