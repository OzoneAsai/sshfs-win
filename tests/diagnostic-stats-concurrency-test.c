#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

struct stats {
    pthread_mutex_t lock;
    uint64_t received;
    uint64_t bytes;
    uint64_t total_rtt;
    unsigned min_rtt;
    unsigned max_rtt;
};

struct worker { struct stats *s; unsigned rtt; unsigned loops; };

static void *record(void *arg)
{
    struct worker *w = arg;
    unsigned i;
    for (i = 0; i < w->loops; i++) {
        pthread_mutex_lock(&w->s->lock);
        if (w->rtt < w->s->min_rtt || !w->s->received)
            w->s->min_rtt = w->rtt;
        if (w->rtt > w->s->max_rtt)
            w->s->max_rtt = w->rtt;
        w->s->total_rtt += w->rtt;
        w->s->received++;
        w->s->bytes += 100 + w->rtt;
        pthread_mutex_unlock(&w->s->lock);
    }
    return NULL;
}

int main(void)
{
    struct stats s = { .lock = PTHREAD_MUTEX_INITIALIZER };
    struct worker a = { &s, 3, 20000 };
    struct worker b = { &s, 11, 20000 };
    pthread_t ta, tb;

    assert(pthread_create(&ta, NULL, record, &a) == 0);
    assert(pthread_create(&tb, NULL, record, &b) == 0);
    assert(pthread_join(ta, NULL) == 0);
    assert(pthread_join(tb, NULL) == 0);
    assert(s.received == 40000);
    assert(s.min_rtt == 3 && s.max_rtt == 11);
    assert(s.total_rtt == (uint64_t)20000 * (3 + 11));
    assert(s.bytes == (uint64_t)20000 * ((100 + 3) + (100 + 11)));
    assert(pthread_mutex_destroy(&s.lock) == 0);
    puts("diagnostic stats synchronization: PASS");
    return 0;
}
