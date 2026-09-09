#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_OPENDIR 11

struct request;
typedef void (*request_func)(struct request *);

struct conn {
    int connver;
    int req_count;
    unsigned remote_handles;
    unsigned remote_handle_peak;
    unsigned remote_handle_reservations;
};

struct request {
    struct conn *conn;
    request_func end_func;
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
static int begin_calls;
static int end_calls;

#include "remote-handle-limit.inc"

static void begin_cb(struct request *req)
{
    (void)req;
    begin_calls++;
}

static void end_cb(struct request *req)
{
    (void)req;
    end_calls++;
}

/* Mirrors the ownership ordering in sftp_request_send after checkpoint 10. */
static int admission_fixture(struct request *req, struct conn *conn,
                             uint8_t type, request_func begin_func,
                             request_func end_func)
{
    int err;

    pthread_mutex_lock(&sshfs.lock);
    req->conn = conn;
    req->conn->req_count++;
    err = remote_handle_reserve_locked(req, type, 1);
    if (err) {
        pthread_mutex_unlock(&sshfs.lock);
        goto out;
    }
    req->end_func = end_func;
    if (begin_func)
        begin_func(req);
    pthread_mutex_unlock(&sshfs.lock);
    return 0;

out:
    if (req->end_func)
        req->end_func(req);
    req->conn->req_count--;
    return err;
}

int main(void)
{
    struct conn conn = { .connver = 1, .remote_handles = 1 };
    struct request accepted = {0};
    struct request rejected = {0};

    assert(pthread_mutex_init(&sshfs.lock, NULL) == 0);
    sshfs.conns = &conn;
    sshfs.handle_limit = 2;

    assert(admission_fixture(&accepted, &conn, SSH_FXP_OPEN,
                             begin_cb, end_cb) == 0);
    assert(begin_calls == 1);
    assert(end_calls == 0);
    assert(conn.remote_handle_reservations == 1);

    /* live=1 + reserved=1 fills the cap: callbacks must never start. */
    assert(admission_fixture(&rejected, &conn, SSH_FXP_OPEN,
                             begin_cb, end_cb) == -EMFILE);
    assert(begin_calls == 1);
    assert(end_calls == 0);
    assert(rejected.end_func == NULL);
    assert(conn.remote_handle_reservations == 1);

    pthread_mutex_lock(&sshfs.lock);
    remote_handle_release_reservation_locked(&accepted);
    pthread_mutex_unlock(&sshfs.lock);
    accepted.end_func(&accepted);
    assert(end_calls == 1);

    /* Exercise the shared accounting path without changing live state. */
    remote_handle_account(&accepted, SSH_FXP_OPEN, -EIO);

    assert(pthread_mutex_destroy(&sshfs.lock) == 0);
    return 0;
}
