#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

struct model {
    pthread_mutex_t global;
    pthread_mutex_t transport;
    pthread_cond_t cv;
    int connver;
    int request_connver;
    int stage;
    int allow_sender;
    int wire_sends;
    int wire_generation;
};

static void must(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

/* Teardown wins after admission but before sender pins transport. */
static void *sender_teardown_first(void *arg)
{
    struct model *m = arg;
    pthread_mutex_lock(&m->global);
    m->request_connver = m->connver;
    m->stage = 1;
    pthread_cond_broadcast(&m->cv);
    while (!m->allow_sender)
        pthread_cond_wait(&m->cv, &m->global);
    pthread_mutex_unlock(&m->global);

    pthread_mutex_lock(&m->transport);
    pthread_mutex_lock(&m->global);
    if (m->request_connver == m->connver) {
        m->wire_sends++;
        m->wire_generation = m->connver;
    }
    pthread_mutex_unlock(&m->global);
    pthread_mutex_unlock(&m->transport);
    return NULL;
}

static void *teardown_first(void *arg)
{
    struct model *m = arg;
    pthread_mutex_lock(&m->global);
    while (m->stage < 1)
        pthread_cond_wait(&m->cv, &m->global);
    pthread_mutex_unlock(&m->global);

    pthread_mutex_lock(&m->transport);
    pthread_mutex_lock(&m->global);
    m->connver++;
    m->allow_sender = 1;
    pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->global);
    pthread_mutex_unlock(&m->transport);
    return NULL;
}

/* Sender wins transport; teardown must wait until old-session wire write ends. */
static void *sender_send_first(void *arg)
{
    struct model *m = arg;
    pthread_mutex_lock(&m->global);
    m->request_connver = m->connver;
    pthread_mutex_unlock(&m->global);

    pthread_mutex_lock(&m->transport);
    pthread_mutex_lock(&m->global);
    must(m->request_connver == m->connver, "sender generation changed before send");
    m->stage = 1;
    pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->global);

    /* Model a wire write while teardown is blocked by transport_lock. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000L };
    nanosleep(&ts, NULL);
    m->wire_sends++;
    m->wire_generation = m->request_connver;
    pthread_mutex_unlock(&m->transport);
    return NULL;
}

static void *teardown_after_send_started(void *arg)
{
    struct model *m = arg;
    pthread_mutex_lock(&m->global);
    while (m->stage < 1)
        pthread_cond_wait(&m->cv, &m->global);
    pthread_mutex_unlock(&m->global);

    pthread_mutex_lock(&m->transport);
    pthread_mutex_lock(&m->global);
    m->connver++;
    pthread_mutex_unlock(&m->global);
    pthread_mutex_unlock(&m->transport);
    return NULL;
}

static void init_model(struct model *m)
{
    *m = (struct model){0};
    pthread_mutex_init(&m->global, NULL);
    pthread_mutex_init(&m->transport, NULL);
    pthread_cond_init(&m->cv, NULL);
    m->connver = 1;
}

static void fini_model(struct model *m)
{
    pthread_cond_destroy(&m->cv);
    pthread_mutex_destroy(&m->transport);
    pthread_mutex_destroy(&m->global);
}

int main(void)
{
    for (int round = 0; round < 100; round++) {
        struct model m;
        pthread_t a, b;
        init_model(&m);
        must(pthread_create(&a, NULL, sender_teardown_first, &m) == 0, "create sender A");
        must(pthread_create(&b, NULL, teardown_first, &m) == 0, "create teardown A");
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        must(m.connver == 2, "teardown-first increments generation");
        must(m.wire_sends == 0, "teardown-first stale request never reaches wire");
        fini_model(&m);

        init_model(&m);
        must(pthread_create(&a, NULL, sender_send_first, &m) == 0, "create sender B");
        must(pthread_create(&b, NULL, teardown_after_send_started, &m) == 0, "create teardown B");
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        must(m.wire_sends == 1, "send-first writes exactly once");
        must(m.wire_generation == 1, "send-first remains on old generation");
        must(m.connver == 2, "teardown occurs after old-session send");
        fini_model(&m);
    }
    puts("transport generation serialization: PASS (100x2 races)");
    return 0;
}
