#include <assert.h>
#include <errno.h>
#include <stdio.h>

struct conn {
    unsigned remote_handles;
    unsigned remote_handle_reservations;
};

static int reserve_open(struct conn *conn, unsigned limit)
{
    if (limit != 0 &&
        conn->remote_handles + conn->remote_handle_reservations >= limit)
        return -EMFILE;
    conn->remote_handle_reservations++;
    return 0;
}

static void complete_open(struct conn *conn)
{
    assert(conn->remote_handle_reservations != 0);
    conn->remote_handle_reservations--;
    conn->remote_handles++;
}

static void complete_close(struct conn *conn)
{
    assert(conn->remote_handles != 0);
    conn->remote_handles--;
}

int main(void)
{
    struct conn conn = { .remote_handles = 1 };
    const unsigned limit = 1;

    /*
     * Old reopen ordering: CLOSE was merely sent, so reply-side accounting
     * still saw the old handle as live when replacement OPENDIR was admitted.
     */
    assert(reserve_open(&conn, limit) == -EMFILE);
    assert(conn.remote_handles == 1);
    assert(conn.remote_handle_reservations == 0);

    /*
     * Replacement ordering: wait for CLOSE STATUS before admitting OPENDIR.
     * The transition never exceeds the cap and rewind works at limit == 1.
     */
    complete_close(&conn);
    assert(conn.remote_handles == 0);
    assert(reserve_open(&conn, limit) == 0);
    assert(conn.remote_handle_reservations == 1);
    complete_open(&conn);
    assert(conn.remote_handles == 1);
    assert(conn.remote_handle_reservations == 0);

    puts("directory reopen handle-limit ordering: PASS");
    return 0;
}
