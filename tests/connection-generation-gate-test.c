#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

struct conn {
    int connver;
    int start_calls;
    int wire_sends;
    int bump_generation_during_start;
    int start_error;
};

static int start_processing_thread(struct conn *conn)
{
    conn->start_calls++;
    if (conn->start_error)
        return conn->start_error;
    if (conn->bump_generation_during_start) {
        conn->connver++;
        conn->bump_generation_during_start = 0;
    }
    return 0;
}

/* Model of the two generation checks in sftp_request_send_gen(). */
static int request_send_gen(struct conn *conn, int expected_connver,
                            int *used_connver)
{
    int err;

    if (used_connver)
        *used_connver = -1;

    /* Pre-start gate: stale opaque handles must not cause reconnect work. */
    if (expected_connver >= 0 && expected_connver != conn->connver)
        return -EIO;

    err = start_processing_thread(conn);
    if (err)
        return err;

    /* Post-start gate: start/reconnect must not change the bound session. */
    if (expected_connver >= 0 && expected_connver != conn->connver)
        return -EIO;

    if (used_connver)
        *used_connver = conn->connver;
    conn->wire_sends++;
    return 0;
}

static void must(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

int main(void)
{
    struct conn c = { .connver = 7 };
    int used = 123;
    int err;

    err = request_send_gen(&c, -1, &used);
    must(err == 0, "unbound request succeeds");
    must(used == 7, "unbound request captures actual generation");
    must(c.wire_sends == 1, "unbound request reaches wire");

    err = request_send_gen(&c, 7, NULL);
    must(err == 0, "same-generation handle request succeeds");
    must(c.wire_sends == 2, "same-generation request reaches wire");

    c.start_calls = 0;
    used = 123;
    err = request_send_gen(&c, 6, &used);
    must(err == -EIO, "stale generation fails closed");
    must(c.start_calls == 0, "stale generation does not start/reconnect");
    must(c.wire_sends == 2, "stale handle never reaches wire");
    must(used == -1, "failed request does not report a generation");

    c.bump_generation_during_start = 1;
    used = 123;
    err = request_send_gen(&c, 7, &used);
    must(err == -EIO, "generation change during start fails closed");
    must(c.connver == 8, "test simulated reconnect generation change");
    must(c.wire_sends == 2, "race-lost old handle never reaches wire");
    must(used == -1, "race-lost request does not publish generation");

    /* A fresh OPEN/OPENDIR is unbound and adopts the new generation. */
    err = request_send_gen(&c, -1, &used);
    must(err == 0, "fresh handle creation may use reconnected session");
    must(used == 8, "fresh handle records new session generation");
    must(c.wire_sends == 3, "fresh handle creation reaches wire");

    /* That object's handle is safe until the generation changes again. */
    err = request_send_gen(&c, used, NULL);
    must(err == 0, "captured generation is valid for handle I/O");
    must(c.wire_sends == 4, "valid handle I/O reaches wire");

    c.connver++;
    err = request_send_gen(&c, used, NULL);
    must(err == -EIO, "old opaque handle rejected after reconnect");
    must(c.wire_sends == 4, "old opaque handle never sent on new session");

    puts("connection generation send gate: PASS");
    return 0;
}
