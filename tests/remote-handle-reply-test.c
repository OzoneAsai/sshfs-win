#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_OPENDIR 11
#define SSH_FXP_STATUS 101
#define SSH_FXP_HANDLE 102
#define SSH_FX_OK 0

struct buffer { unsigned char *p; size_t len; size_t size; };
struct conn { unsigned remote_handles, remote_handle_peak, remote_handle_reservations; int connver; };
struct request {
    uint8_t request_type;
    struct conn *conn;
    int remote_handle_reserved;
    int remote_handle_reservation_connver;
};
struct sshfs_state { struct conn *conns; unsigned handle_warn; };
static struct sshfs_state sshfs;

struct remote_handle_warning { int emit; unsigned conn_index, tracked, peak, warning_step; };

static int buf_get_uint32(struct buffer *b, uint32_t *out)
{
    if (b->len + 4 > b->size) return -1;
    unsigned char *p = b->p + b->len;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    b->len += 4;
    return 0;
}


static int handle_reply_valid(const struct buffer *reply)
{
    struct buffer copy = *reply;
    uint32_t len;
    if (copy.len > copy.size || buf_get_uint32(&copy, &len) == -1) return 0;
    return len == copy.size - copy.len;
}

static void remote_handle_release_reservation_locked(struct request *req)
{
    struct conn *conn;
    if (!req->remote_handle_reserved) return;
    conn = req->conn;
    if (req->remote_handle_reservation_connver == conn->connver) {
        assert(conn->remote_handle_reservations != 0);
        conn->remote_handle_reservations--;
    }
    req->remote_handle_reserved = 0;
}

static void account(struct request *req, uint8_t reply_type,
                    const struct buffer *reply, struct remote_handle_warning *warning)
{
    struct conn *conn = req->conn;
    int creates_handle = 0, closes_handle = 0;
    unsigned old_count;

    if (req->request_type == SSH_FXP_OPEN || req->request_type == SSH_FXP_OPENDIR) {
        remote_handle_release_reservation_locked(req);
        creates_handle = reply_type == SSH_FXP_HANDLE && handle_reply_valid(reply);
    } else if (req->request_type == SSH_FXP_CLOSE) {
        struct buffer copy = *reply;
        uint32_t status;
        if (reply_type == SSH_FXP_STATUS && buf_get_uint32(&copy, &status) != -1 && status == SSH_FX_OK)
            closes_handle = 1;
    } else return;

    if (!creates_handle && !closes_handle) return;
    old_count = conn->remote_handles;
    if (creates_handle) {
        conn->remote_handles++;
        if (conn->remote_handles > conn->remote_handle_peak) conn->remote_handle_peak = conn->remote_handles;
        if (sshfs.handle_warn != 0 && conn->remote_handles / sshfs.handle_warn > old_count / sshfs.handle_warn) {
            warning->emit = 1;
            warning->conn_index = (unsigned)(conn - sshfs.conns);
            warning->tracked = conn->remote_handles;
            warning->peak = conn->remote_handle_peak;
            warning->warning_step = sshfs.handle_warn;
        }
    } else if (conn->remote_handles != 0) conn->remote_handles--;
}

static struct buffer status_buf(uint32_t status, unsigned char bytes[4])
{
    bytes[0] = status >> 24; bytes[1] = status >> 16; bytes[2] = status >> 8; bytes[3] = status;
    return (struct buffer){ .p = bytes, .len = 0, .size = 4 };
}

int main(void)
{
    struct conn conns[2] = {0};
    sshfs.conns = conns; sshfs.handle_warn = 2;

    struct request o = { .request_type = SSH_FXP_OPEN, .conn = &conns[0], .remote_handle_reserved=1, .remote_handle_reservation_connver=0 };
    conns[0].remote_handle_reservations = 1;
    struct remote_handle_warning w = {0};
    unsigned char hb[] = {0,0,0,2,'h','1'};
    struct buffer handle_reply = { .p = hb, .len = 0, .size = sizeof hb };
    account(&o, SSH_FXP_HANDLE, &handle_reply, &w);
    assert(conns[0].remote_handles == 1 && conns[0].remote_handle_reservations == 0 && !o.remote_handle_reserved);

    struct request d = { .request_type = SSH_FXP_OPENDIR, .conn = &conns[0], .remote_handle_reserved=1, .remote_handle_reservation_connver=0 };
    conns[0].remote_handle_reservations = 1; memset(&w,0,sizeof(w));
    account(&d, SSH_FXP_HANDLE, &handle_reply, &w);
    assert(conns[0].remote_handles == 2 && w.emit && w.tracked == 2);

    /* HANDLE type alone is not success: malformed payload creates no phantom handle. */
    struct request badh = { .request_type = SSH_FXP_OPEN, .conn = &conns[1], .remote_handle_reserved=1, .remote_handle_reservation_connver=0 };
    unsigned char malformed_bytes[] = {0,0,0,5,'x'};
    struct buffer malformed = { .p = malformed_bytes, .len = 0, .size = sizeof malformed_bytes };
    conns[1].remote_handle_reservations = 1; memset(&w,0,sizeof(w));
    account(&badh, SSH_FXP_HANDLE, &malformed, &w);
    assert(conns[1].remote_handles == 0);
    assert(conns[1].remote_handle_reservations == 0);

    /* CLOSE is asynchronous in SSHFS; accounting must happen from the reply path, not sftp_request_wait(). */
    unsigned char okb[4]; struct buffer ok = status_buf(SSH_FX_OK, okb);
    struct request c = { .request_type = SSH_FXP_CLOSE, .conn = &conns[0] }; memset(&w,0,sizeof(w));
    account(&c, SSH_FXP_STATUS, &ok, &w);
    assert(conns[0].remote_handles == 1);
    assert(ok.len == 0); /* parser is non-destructive to the real reply */

    unsigned char failb[4]; struct buffer fail = status_buf(4, failb);
    account(&c, SSH_FXP_STATUS, &fail, &w);
    assert(conns[0].remote_handles == 1); /* failed CLOSE stays conservative */

    /* Failed OPEN releases reservation but does not create a live handle. */
    struct request fo = { .request_type = SSH_FXP_OPEN, .conn = &conns[1], .remote_handle_reserved=1, .remote_handle_reservation_connver=0 };
    conns[1].remote_handle_reservations = 1;
    account(&fo, SSH_FXP_STATUS, &fail, &w);
    assert(conns[1].remote_handles == 0 && conns[1].remote_handle_reservations == 0);

    /* Stale-generation reservation never damages the current generation. */
    struct request stale = { .request_type = SSH_FXP_OPEN, .conn = &conns[1], .remote_handle_reserved=1, .remote_handle_reservation_connver=0 };
    conns[1].connver = 1; conns[1].remote_handle_reservations = 3;
    account(&stale, SSH_FXP_STATUS, &fail, &w);
    assert(conns[1].remote_handle_reservations == 3 && !stale.remote_handle_reserved);

    puts("remote handle reply accounting: PASS");
    return 0;
}
