#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_OPENDIR 11

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

int main(void)
{
    struct conn conns[1] = {0};
    struct request req[8] = {0};
    unsigned i;

    assert(pthread_mutex_init(&sshfs.lock, NULL) == 0);
    sshfs.conns = conns;
    sshfs.handle_warn = 0;
    sshfs.handle_limit = 4;
    for (i = 0; i < 8; i++)
        req[i].conn = &conns[0];

    /* Four concurrent admissions fill the cap using reservations alone. */
    pthread_mutex_lock(&sshfs.lock);
    for (i = 0; i < 4; i++)
        assert(remote_handle_reserve_locked(&req[i], SSH_FXP_OPEN, 1) == 0);
    assert(conns[0].remote_handle_reservations == 4);
    assert(remote_handle_reserve_locked(&req[4], SSH_FXP_OPEN, 1) == -EMFILE);
    pthread_mutex_unlock(&sshfs.lock);

    /* Successful replies convert reservations to live handles, never > 4. */
    for (i = 0; i < 4; i++) {
        remote_handle_account(&req[i], SSH_FXP_OPEN, 0);
        assert(conns[0].remote_handles + conns[0].remote_handle_reservations == 4);
    }
    assert(conns[0].remote_handles == 4);
    assert(conns[0].remote_handle_peak == 4);

    /* Still capped until a CLOSE succeeds. */
    pthread_mutex_lock(&sshfs.lock);
    assert(remote_handle_reserve_locked(&req[4], SSH_FXP_OPENDIR, 1) == -EMFILE);
    pthread_mutex_unlock(&sshfs.lock);

    remote_handle_account(&req[0], SSH_FXP_CLOSE, 0);
    assert(conns[0].remote_handles == 3);

    pthread_mutex_lock(&sshfs.lock);
    assert(remote_handle_reserve_locked(&req[4], SSH_FXP_OPENDIR, 1) == 0);
    pthread_mutex_unlock(&sshfs.lock);
    assert(conns[0].remote_handle_reservations == 1);

    /* Failed admitted OPEN releases its reservation without creating a handle. */
    remote_handle_account(&req[4], SSH_FXP_OPENDIR, -EIO);
    assert(conns[0].remote_handle_reservations == 0);
    assert(conns[0].remote_handles == 3);


    /* Teardown owns and releases a pending reservation before wakeup. */
    sshfs.handle_limit = 4;
    pthread_mutex_lock(&sshfs.lock);
    assert(remote_handle_reserve_locked(&req[6], SSH_FXP_OPEN, 1) == 0);
    assert(conns[0].remote_handle_reservations == 1);
    remote_handle_release_reservation_locked(&req[6]);
    assert(conns[0].remote_handle_reservations == 0);
    pthread_mutex_unlock(&sshfs.lock);

    /* A waiter accounting the teardown error is now idempotent. */
    remote_handle_account(&req[6], SSH_FXP_OPEN, -EIO);
    assert(conns[0].remote_handle_reservations == 0);
    assert(conns[0].remote_handles == 3);

    /*
     * A reservation from a dead connection generation must never decrement a
     * reservation belonging to the replacement generation.
     */
    pthread_mutex_lock(&sshfs.lock);
    conns[0].connver = 7;
    assert(remote_handle_reserve_locked(&req[7], SSH_FXP_OPEN, 1) == 0);
    assert(req[7].remote_handle_reservation_connver == 7);
    assert(conns[0].remote_handle_reservations == 1);

    /* Teardown rolls generation and discards old-generation reservations. */
    conns[0].connver = 8;
    conns[0].remote_handle_reservations = 0;
    assert(remote_handle_reserve_locked(&req[5], SSH_FXP_OPEN, 1) == 0);
    assert(conns[0].remote_handle_reservations == 1);

    /* Late cleanup from generation 7 clears only its request-local bit. */
    remote_handle_release_reservation_locked(&req[7]);
    assert(req[7].remote_handle_reserved == 0);
    assert(conns[0].remote_handle_reservations == 1);

    remote_handle_release_reservation_locked(&req[5]);
    assert(conns[0].remote_handle_reservations == 0);
    pthread_mutex_unlock(&sshfs.lock);

    /* Limit zero disables admission control. */
    sshfs.handle_limit = 0;
    pthread_mutex_lock(&sshfs.lock);
    assert(remote_handle_reserve_locked(&req[5], SSH_FXP_OPEN, 1) == 0);
    pthread_mutex_unlock(&sshfs.lock);
    assert(req[5].remote_handle_reserved == 0);

    assert(pthread_mutex_destroy(&sshfs.lock) == 0);
    return 0;
}
