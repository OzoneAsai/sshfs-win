#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_OPENDIR 11

struct conn {
    unsigned remote_handles;
    unsigned remote_handle_peak;
};

struct request {
    struct conn *conn;
};

struct sshfs_state {
    pthread_mutex_t lock;
    struct conn *conns;
    unsigned handle_warn;
};

static struct sshfs_state sshfs;

#include "remote-handle-accounting.inc"

int main(void)
{
    struct conn conns[2] = {0};
    struct request r0 = { .conn = &conns[0] };
    struct request r1 = { .conn = &conns[1] };

    assert(pthread_mutex_init(&sshfs.lock, NULL) == 0);
    sshfs.conns = conns;
    sshfs.handle_warn = 4;

    remote_handle_account(&r0, SSH_FXP_OPEN, 0);
    remote_handle_account(&r0, SSH_FXP_OPEN, 0);
    remote_handle_account(&r0, SSH_FXP_OPENDIR, 0);
    assert(conns[0].remote_handles == 3);
    assert(conns[0].remote_handle_peak == 3);

    remote_handle_account(&r0, SSH_FXP_OPEN, 0);
    assert(conns[0].remote_handles == 4);
    assert(conns[0].remote_handle_peak == 4);

    remote_handle_account(&r0, SSH_FXP_CLOSE, 0);
    assert(conns[0].remote_handles == 3);

    /* Failed CLOSE is conservative: keep it counted. */
    remote_handle_account(&r0, SSH_FXP_CLOSE, -5);
    assert(conns[0].remote_handles == 3);

    remote_handle_account(&r1, SSH_FXP_OPENDIR, 0);
    assert(conns[1].remote_handles == 1);
    assert(conns[0].remote_handles == 3);

    remote_handle_account(&r0, SSH_FXP_CLOSE, 0);
    remote_handle_account(&r0, SSH_FXP_CLOSE, 0);
    remote_handle_account(&r0, SSH_FXP_CLOSE, 0);
    assert(conns[0].remote_handles == 0);

    /* A duplicate/late CLOSE must never underflow. */
    remote_handle_account(&r0, SSH_FXP_CLOSE, 0);
    assert(conns[0].remote_handles == 0);
    assert(conns[0].remote_handle_peak == 4);

    /* Non-handle operations are ignored. */
    remote_handle_account(&r0, 99, 0);
    assert(conns[0].remote_handles == 0);

    assert(pthread_mutex_destroy(&sshfs.lock) == 0);
    return 0;
}
