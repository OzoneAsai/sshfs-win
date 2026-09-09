#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

struct shrink_result {
    int result;
    int destructive_open;
    size_t bytes_preserved;
    size_t bytes_rewritten;
};

static size_t calc_chunk(uint64_t size, uint64_t offset, size_t max_read)
{
    uint64_t remaining = size - offset;
    return remaining < max_read ? (size_t)remaining : max_read;
}

/* Model CP31-Z: preservation must complete before the O_TRUNC boundary. */
static struct shrink_result shrink_model(size_t target,
                                         const int *reads, size_t nreads,
                                         int read_release,
                                         const int *writes, size_t nwrites,
                                         int flush_result, int write_release)
{
    struct shrink_result out = {0, 0, 0, 0};
    size_t i = 0;

    while (out.bytes_preserved < target) {
        int r;
        if (i >= nreads) {
            out.result = -EIO;
            return out;
        }
        r = reads[i++];
        if (r < 0) {
            out.result = r;
            return out;
        }
        if (r == 0) {
            out.result = -EIO;
            return out;
        }
        assert((size_t)r <= target - out.bytes_preserved);
        out.bytes_preserved += (size_t)r;
    }
    if (read_release) {
        out.result = read_release;
        return out;
    }

    out.destructive_open = 1;
    i = 0;
    while (out.bytes_rewritten < target) {
        int r;
        if (i >= nwrites) {
            out.result = -EIO;
            return out;
        }
        r = writes[i++];
        if (r < 0) {
            out.result = r;
            return out;
        }
        if (r == 0) {
            out.result = -EIO;
            return out;
        }
        assert((size_t)r <= target - out.bytes_rewritten);
        out.bytes_rewritten += (size_t)r;
    }
    if (flush_result) {
        out.result = flush_result;
        return out;
    }
    if (write_release) {
        out.result = write_release;
        return out;
    }
    return out;
}

int main(void)
{
    const int full_reads[] = {4, 4, 2};
    const int short_eof[] = {4, 0};
    const int read_error[] = {4, -EIO};
    const int full_writes[] = {4, 4, 2};
    const int zero_write[] = {4, 0};
    struct shrink_result r;

    r = shrink_model(10, full_reads, 3, 0, full_writes, 3, 0, 0);
    assert(r.result == 0);
    assert(r.destructive_open == 1);
    assert(r.bytes_preserved == 10 && r.bytes_rewritten == 10);

    /* Premature EOF must not cross the O_TRUNC boundary. */
    r = shrink_model(10, short_eof, 2, 0, full_writes, 3, 0, 0);
    assert(r.result == -EIO);
    assert(r.destructive_open == 0);
    assert(r.bytes_preserved == 4);

    r = shrink_model(10, read_error, 2, 0, full_writes, 3, 0, 0);
    assert(r.result == -EIO);
    assert(r.destructive_open == 0);

    /* A failed close of the preservation handle is also pre-destructive. */
    r = shrink_model(10, full_reads, 3, -EIO, full_writes, 3, 0, 0);
    assert(r.result == -EIO);
    assert(r.destructive_open == 0);

    /* Once destructive rewrite begins, zero progress must fail, not spin. */
    r = shrink_model(10, full_reads, 3, 0, zero_write, 2, 0, 0);
    assert(r.result == -EIO);
    assert(r.destructive_open == 1);
    assert(r.bytes_rewritten == 4);

    r = shrink_model(10, full_reads, 3, 0, full_writes, 3, -ENOSPC, 0);
    assert(r.result == -ENOSPC);

    r = shrink_model(10, full_reads, 3, 0, full_writes, 3, 0, -EIO);
    assert(r.result == -EIO);

    /* Remaining-based sizing cannot overflow offset + max_read. */
    assert(calc_chunk(UINT64_MAX, UINT64_MAX - 3, 65536) == 3);
    assert(calc_chunk(100, 96, 64) == 4);
    assert(calc_chunk(100, 32, 64) == 64);

    puts("truncate-workaround-preservation-test: PASS");
    return 0;
}
