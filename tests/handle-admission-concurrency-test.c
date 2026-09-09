#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_OPENDIR 11
#define THREADS 64
#define LIMIT 8
#define ROUNDS 50

struct conn {
    int connver;
    unsigned remote_handles;
    unsigned remote_handle_peak;
    unsigned remote_handle_reservations;
};
struct request {
    struct conn *conn;
    int remote_handle_reserved;
    int remote_handle_reservation_connver;
};
struct sshfs_state {
    pthread_mutex_t lock;
    struct conn *conns;
    unsigned handle_warn;
    unsigned handle_limit;
};
static struct sshfs_state sshfs;
#include "remote-handle-limit.inc"

static pthread_barrier_t barrier;
static unsigned accepted;
static pthread_mutex_t accepted_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    struct request *req = arg;
    int r;

    {
        int br = pthread_barrier_wait(&barrier);
        assert(br == 0 || br == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    pthread_mutex_lock(&sshfs.lock);
    r = remote_handle_reserve_locked(req, SSH_FXP_OPEN, 1);
    pthread_mutex_unlock(&sshfs.lock);
    if (r == 0) {
        pthread_mutex_lock(&accepted_lock);
        accepted++;
        pthread_mutex_unlock(&accepted_lock);
        remote_handle_account(req, SSH_FXP_OPEN, 0);
    } else {
        assert(r == -EMFILE);
    }
    return NULL;
}

int main(void)
{
    unsigned round;
    assert(pthread_mutex_init(&sshfs.lock, NULL) == 0);
    sshfs.handle_warn = 0;
    sshfs.handle_limit = LIMIT;

    for (round = 0; round < ROUNDS; round++) {
        struct conn conn = { .connver = (int)round + 1 };
        struct request req[THREADS] = {0};
        pthread_t th[THREADS];
        unsigned i;
        sshfs.conns = &conn;
        accepted = 0;
        assert(pthread_barrier_init(&barrier, NULL, THREADS) == 0);
        for (i = 0; i < THREADS; i++) {
            req[i].conn = &conn;
            assert(pthread_create(&th[i], NULL, worker, &req[i]) == 0);
        }
        for (i = 0; i < THREADS; i++)
            assert(pthread_join(th[i], NULL) == 0);
        assert(pthread_barrier_destroy(&barrier) == 0);
        assert(accepted == LIMIT);
        assert(conn.remote_handles == LIMIT);
        assert(conn.remote_handle_reservations == 0);
        assert(conn.remote_handle_peak == LIMIT);
    }
    assert(pthread_mutex_destroy(&sshfs.lock) == 0);
    puts("concurrent handle admission cap: PASS");
    return 0;
}
