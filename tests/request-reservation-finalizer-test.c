#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
static int end_calls;

#include "remote-handle-limit.inc"

static void end_cb(struct request *req)
{
    (void)req;
    end_calls++;
}

/* Mirrors the reservation-relevant part of production request_free(). */
static void request_free_fixture(struct request *req)
{
    remote_handle_release_reservation_locked(req);
    if (req->end_func)
        req->end_func(req);
    req->conn->req_count--;
}

int main(void)
{
    struct conn conn = { .connver = 10 };
    struct request setup_fail = { .conn = &conn };
    struct request replied = { .conn = &conn };
    struct request stale = { .conn = &conn };
    struct request replacement = { .conn = &conn };

    assert(pthread_mutex_init(&sshfs.lock, NULL) == 0);
    sshfs.conns = &conn;
    sshfs.handle_limit = 8;

    /*
     * Local startup failure after admission: the request never reaches reqtab
     * and never receives an OPEN reply. Final request destruction must release
     * the reservation or repeated connection failures poison the cap.
     */
    pthread_mutex_lock(&sshfs.lock);
    conn.req_count++;
    assert(remote_handle_reserve_locked(&setup_fail, SSH_FXP_OPEN, 1) == 0);
    setup_fail.end_func = end_cb; /* begin/end ownership already armed */
    assert(conn.remote_handle_reservations == 1);
    request_free_fixture(&setup_fail);
    assert(conn.remote_handle_reservations == 0);
    assert(conn.req_count == 0);
    assert(end_calls == 1);
    pthread_mutex_unlock(&sshfs.lock);

    /* Normal reply accounting releases first; finalizer is idempotent. */
    pthread_mutex_lock(&sshfs.lock);
    conn.req_count++;
    assert(remote_handle_reserve_locked(&replied, SSH_FXP_OPEN, 1) == 0);
    pthread_mutex_unlock(&sshfs.lock);
    remote_handle_account(&replied, SSH_FXP_OPEN, 0);
    assert(conn.remote_handle_reservations == 0);
    assert(conn.remote_handles == 1);
    pthread_mutex_lock(&sshfs.lock);
    request_free_fixture(&replied);
    assert(conn.remote_handle_reservations == 0);
    assert(conn.req_count == 0);
    pthread_mutex_unlock(&sshfs.lock);

    /*
     * A stale request from an old connection generation may be destroyed after
     * a replacement generation has admitted another OPEN. It must not steal
     * the replacement generation's reservation.
     */
    pthread_mutex_lock(&sshfs.lock);
    conn.connver = 20;
    conn.req_count++;
    assert(remote_handle_reserve_locked(&stale, SSH_FXP_OPEN, 1) == 0);
    assert(stale.remote_handle_reservation_connver == 20);

    conn.connver = 21;
    conn.remote_handle_reservations = 0; /* teardown discards generation 20 */
    conn.req_count = 1; /* only the stale request remains locally for this model */
    assert(remote_handle_reserve_locked(&replacement, SSH_FXP_OPEN, 1) == 0);
    assert(conn.remote_handle_reservations == 1);

    request_free_fixture(&stale);
    assert(!stale.remote_handle_reserved);
    assert(conn.remote_handle_reservations == 1);

    /* Cleanup replacement through the same finalizer. */
    conn.req_count++;
    request_free_fixture(&replacement);
    assert(conn.remote_handle_reservations == 0);
    pthread_mutex_unlock(&sshfs.lock);

    assert(pthread_mutex_destroy(&sshfs.lock) == 0);
    puts("request reservation finalizer PASS");
    return 0;
}
