#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

struct conn {
    int connected;
    int connver;
    int load;
    uint32_t server_version;
    int ext_posix_rename;
    int ext_statvfs;
    int ext_hardlink;
    int ext_fsync;
};

struct features {
    struct conn *conn;
    int connver;
    uint32_t server_version;
    int ext_posix_rename;
    int ext_statvfs;
    int ext_hardlink;
};

static int connect_calls;

static void negotiate(struct conn *c)
{
    connect_calls++;
    c->connected = 1;
    c->server_version = 3;
    c->ext_posix_rename = 1;
    c->ext_statvfs = 1;
    c->ext_hardlink = 1;
    c->ext_fsync = 1;
}

static void reconnect_begin(struct conn *c)
{
    c->connected = 0;
    c->connver++;
    c->server_version = 0;
    c->ext_posix_rename = 0;
    c->ext_statvfs = 0;
    c->ext_hardlink = 0;
    c->ext_fsync = 0;
}

static struct conn *choose_conn(struct conn *conns, int n)
{
    int i, best = 0;
    for (i = 1; i < n; i++)
        if (conns[i].load < conns[best].load)
            best = i;
    return &conns[best];
}

static int get_features(struct conn *conns, int n, struct features *f,
                        int ensure_connected)
{
    struct conn *c = choose_conn(conns, n);
    if (ensure_connected && !c->connected)
        negotiate(c);
    f->conn = c;
    f->connver = c->connver;
    f->server_version = c->server_version;
    f->ext_posix_rename = c->ext_posix_rename;
    f->ext_statvfs = c->ext_statvfs;
    f->ext_hardlink = c->ext_hardlink;
    return 0;
}

static int statfs_can_use_extension(struct conn *conns, int n)
{
    struct features f;
    int err = get_features(conns, n, &f, 1);
    if (err)
        return err;
    return f.ext_statvfs ? 1 : 0;
}

static int fsync_handle(struct conn *c, int handle_connver)
{
    if (handle_connver != c->connver)
        return -EIO;
    return c->ext_fsync ? 1 : 0;
}

int main(void)
{
    struct conn conns[2] = {
        { .connected = 1, .connver = 7, .load = 10,
          .server_version = 3, .ext_statvfs = 1, .ext_fsync = 1 },
        { .connected = 0, .connver = 8, .load = 0 },
    };

    /* Capability decisions must establish the selected lazy connection first. */
    assert(statfs_can_use_extension(conns, 2) == 1);
    assert(connect_calls == 1);
    assert(conns[1].connected == 1);
    assert(conns[1].ext_statvfs == 1);

    /* Reconnect starts a fresh capability domain; stale flags cannot survive. */
    reconnect_begin(&conns[1]);
    assert(conns[1].server_version == 0);
    assert(conns[1].ext_statvfs == 0);
    assert(conns[1].ext_fsync == 0);
    assert(statfs_can_use_extension(conns, 2) == 1);
    assert(connect_calls == 2);

    /* Handle-bound fsync uses the handle's session capability and generation. */
    assert(fsync_handle(&conns[1], conns[1].connver) == 1);
    reconnect_begin(&conns[1]);
    assert(fsync_handle(&conns[1], conns[1].connver - 1) == -EIO);

    puts("per-connection capabilities: PASS");
    return 0;
}
