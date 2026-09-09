#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#define MY_EOF (-4096)

struct conn {
    int connver;
};

struct request {
    unsigned id;
    struct conn *conn;
    int connver;
};

struct read_req {
    size_t size;
    long res;
    int completed;
};

static int reply_identity_ok(const struct request *req, const struct conn *rx)
{
    return req != NULL && req->conn == rx && req->connver == rx->connver;
}

static long read_result(struct read_req *reqs, size_t count, size_t wanted)
{
    long res = 0;
    int error = 0;
    size_t i;

    for (i = 0; i < count && wanted; i++) {
        struct read_req *r = &reqs[i];
        if (!r->completed) {
            error = -EIO;
            break;
        }
        if (r->res < 0) {
            error = (int)r->res;
            break;
        }
        if (r->res == 0) {
            error = MY_EOF;
            break;
        }
        if (wanted < (size_t)r->res) {
            res += (long)wanted;
            wanted = 0;
            break;
        }
        res += r->res;
        wanted -= (size_t)r->res;
        if ((size_t)r->res < r->size) {
            error = MY_EOF;
            break;
        }
    }

    if (res == 0 && error && error != MY_EOF)
        return error;
    return res;
}

static int release_result(int flush_err, int current_generation, int close_err,
                          int *flush_calls, int *close_calls)
{
    int err;

    (*flush_calls)++;
    err = flush_err;
    if (current_generation) {
        (*close_calls)++;
        if (!err && close_err)
            err = close_err;
    }
    return err;
}

int main(void)
{
    struct conn a = { .connver = 7 };
    struct conn b = { .connver = 9 };
    struct request req = { .id = 42, .conn = &a, .connver = 7 };
    struct read_req r[2];
    int flush_calls = 0, close_calls = 0;

    assert(reply_identity_ok(&req, &a));
    assert(!reply_identity_ok(&req, &b));
    a.connver++;
    assert(!reply_identity_ok(&req, &a));
    assert(!reply_identity_ok(NULL, &a));
    a.connver--;

    r[0] = (struct read_req){ .size = 32, .res = -EACCES, .completed = 1 };
    assert(read_result(r, 1, 32) == -EACCES);

    r[0] = (struct read_req){ .size = 32, .res = 0, .completed = 1 };
    assert(read_result(r, 1, 32) == 0);

    r[0] = (struct read_req){ .size = 32, .res = -EIO, .completed = 0 };
    assert(read_result(r, 1, 32) == -EIO);

    /* A completed prefix may be returned; the later failure must not erase it. */
    r[0] = (struct read_req){ .size = 16, .res = 16, .completed = 1 };
    r[1] = (struct read_req){ .size = 16, .res = -EIO, .completed = 1 };
    assert(read_result(r, 2, 32) == 16);

    /* Stale generations still execute flush so disconnect/write errors surface. */
    assert(release_result(-EIO, 0, 0, &flush_calls, &close_calls) == -EIO);
    assert(flush_calls == 1 && close_calls == 0);

    /* A local CLOSE send failure is visible when flush itself succeeded. */
    assert(release_result(0, 1, -EPIPE, &flush_calls, &close_calls) == -EPIPE);
    assert(flush_calls == 2 && close_calls == 1);

    /* Earlier write/flush failure takes precedence while CLOSE is still attempted. */
    assert(release_result(-ENOSPC, 1, -EPIPE, &flush_calls, &close_calls) == -ENOSPC);
    assert(flush_calls == 3 && close_calls == 2);

    puts("read/reply/release contract: PASS");
    return 0;
}
