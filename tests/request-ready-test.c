#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef sem_t request_ready_t;

static int request_ready_init(request_ready_t *ready)
{
    return sem_init(ready, 0, 0);
}

static void request_ready_post(request_ready_t *ready)
{
    if (sem_post(ready) == -1) {
        perror("sem_post(request ready)");
        abort();
    }
}

static void request_ready_wait(request_ready_t *ready)
{
    int res;

    do {
        res = sem_wait(ready);
    } while (res == -1 && errno == EINTR);

    if (res == -1) {
        perror("sem_wait(request ready)");
        abort();
    }
}

static void request_ready_destroy(request_ready_t *ready)
{
    if (sem_destroy(ready) == -1) {
        perror("sem_destroy(request ready)");
        abort();
    }
}

struct waiter_ctx {
    request_ready_t *ready;
    volatile sig_atomic_t completed;
};

static void noop_handler(int sig)
{
    (void)sig;
}

static void *waiter(void *arg)
{
    struct waiter_ctx *ctx = arg;
    request_ready_wait(ctx->ready);
    ctx->completed = 1;
    return NULL;
}

int main(void)
{
    request_ready_t ready;
    struct waiter_ctx ctx = { .ready = &ready, .completed = 0 };
    struct sigaction sa = { 0 };
    pthread_t thread;

    assert(request_ready_init(&ready) == 0);

    sa.sa_handler = noop_handler;
    sigemptyset(&sa.sa_mask);
    assert(sigaction(SIGUSR1, &sa, NULL) == 0);

    assert(pthread_create(&thread, NULL, waiter, &ctx) == 0);
    usleep(20000);

    /* Exercise the EINTR retry path before the real completion signal. */
    assert(pthread_kill(thread, SIGUSR1) == 0);
    usleep(20000);
    assert(ctx.completed == 0);

    request_ready_post(&ready);
    assert(pthread_join(thread, NULL) == 0);
    assert(ctx.completed == 1);

    request_ready_destroy(&ready);
    puts("request-ready lifecycle: PASS");
    return 0;
}
