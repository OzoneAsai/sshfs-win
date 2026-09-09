#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct write_result {
    int result;
    size_t requests_sent;
    size_t bytes_committed;
};

/* Model the CP31-U safe-write rule: one remote STATUS barrier per chunk. */
static struct write_result sync_write_model(size_t size, size_t max_write,
                                            const int *status, size_t nstatus)
{
    struct write_result out = {0, 0, 0};
    size_t remaining = size;

    assert(max_write != 0);
    while (remaining) {
        size_t chunk = remaining < max_write ? remaining : max_write;
        int err;

        assert(out.requests_sent < nstatus);
        err = status[out.requests_sent++];
        if (err) {
            out.result = out.bytes_committed ? (int)out.bytes_committed : err;
            return out;
        }
        out.bytes_committed += chunk;
        remaining -= chunk;
    }
    out.result = (int)out.bytes_committed;
    return out;
}

static size_t append_committed(char *dst, size_t dstlen, const char *src,
                               size_t srclen, size_t max_write,
                               const int *status, size_t nstatus,
                               int *result)
{
    struct write_result wr = sync_write_model(srclen, max_write, status, nstatus);
    assert(dstlen >= wr.bytes_committed);
    memcpy(dst, src, wr.bytes_committed);
    *result = wr.result;
    return wr.bytes_committed;
}

int main(void)
{
    const size_t chunk = 4;
    const int ok3[] = {0, 0, 0};
    const int fail_second[] = {0, -ENOSPC, 0};
    const int fail_first[] = {-EIO, 0, 0};
    struct write_result wr;

    wr = sync_write_model(10, chunk, ok3, 3);
    assert(wr.result == 10);
    assert(wr.requests_sent == 3);
    assert(wr.bytes_committed == 10);

    wr = sync_write_model(10, chunk, fail_second, 3);
    assert(wr.result == 4);              /* committed prefix, not all-or-error */
    assert(wr.requests_sent == 2);       /* chunk 3 must never reach the wire */
    assert(wr.bytes_committed == 4);

    wr = sync_write_model(10, chunk, fail_first, 3);
    assert(wr.result == -EIO);           /* no remote mutation: preserve errno */
    assert(wr.requests_sent == 1);
    assert(wr.bytes_committed == 0);

    /* O_APPEND retry must not duplicate the already committed prefix. */
    {
        const char payload[] = "ABCDEFGHIJ";
        char remote[sizeof(payload)] = {0};
        size_t remote_len = 0;
        int result;
        const int retry_ok[] = {0, 0};

        remote_len += append_committed(remote + remote_len,
                                       sizeof(remote) - remote_len,
                                       payload, 10, chunk,
                                       fail_second, 3, &result);
        assert(result == 4);
        assert(remote_len == 4);

        remote_len += append_committed(remote + remote_len,
                                       sizeof(remote) - remote_len,
                                       payload + result, 10 - (size_t)result,
                                       chunk, retry_ok, 2, &result);
        assert(result == 6);
        assert(remote_len == 10);
        assert(memcmp(remote, payload, 10) == 0);
    }

    puts("sync-write-commit-contract-test: PASS");
    return 0;
}
