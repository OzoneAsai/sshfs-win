#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

struct model {
    pthread_mutex_t lock;
    sem_t ready;
    int error;
};

static int error_snapshot(struct model *m)
{
    int err;
    pthread_mutex_lock(&m->lock);
    err = m->error;
    pthread_mutex_unlock(&m->lock);
    return err;
}

static int wait_result(struct model *m)
{
    int err = error_snapshot(m);
    if (err)
        return err;

    while (sem_wait(&m->ready) == -1) {
        if (errno != EINTR)
            return -errno;
    }

    return error_snapshot(m);
}

static void *disconnect_writer(void *arg)
{
    struct model *m = arg;
    pthread_mutex_lock(&m->lock);
    m->error = -EIO;
    pthread_mutex_unlock(&m->lock);
    assert(sem_post(&m->ready) == 0);
    return NULL;
}

int main(void)
{
    struct model m;
    pthread_t th;

    assert(pthread_mutex_init(&m.lock, NULL) == 0);
    assert(sem_init(&m.ready, 0, 0) == 0);

    /* Local failure happens before any completion post. */
    m.error = -EPIPE;
    assert(wait_result(&m) == -EPIPE);

    /* Teardown may race with the waiter, but the error observation is locked. */
    m.error = 0;
    assert(pthread_create(&th, NULL, disconnect_writer, &m) == 0);
    assert(wait_result(&m) == -EIO);
    assert(pthread_join(th, NULL) == 0);

    assert(sem_destroy(&m.ready) == 0);
    assert(pthread_mutex_destroy(&m.lock) == 0);
    puts("request error synchronization: PASS");
    return 0;
}
