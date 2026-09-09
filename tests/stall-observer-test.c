#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct conn_state {
    uint64_t progress_epoch;
};

struct req_state {
    unsigned conn_index;
    uint32_t id;
    struct timespec start;
};

struct scan_state {
    uint64_t oldest_ms[2];
    uint32_t oldest_id[2];
    unsigned count[2];
};

static uint64_t timespec_age_ms(const struct timespec *now,
                                const struct timespec *start)
{
    time_t sec;
    long nsec;

    if (start->tv_sec == 0 && start->tv_nsec == 0)
        return 0;
    sec = now->tv_sec - start->tv_sec;
    nsec = now->tv_nsec - start->tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    if (sec < 0)
        return 0;
    return (uint64_t) sec * 1000 + (uint64_t) nsec / 1000000;
}

static void scan_req(const struct timespec *now, const struct req_state *req,
                     struct scan_state *scan)
{
    uint64_t age;
    unsigned i = req->conn_index;

    assert(i < 2);
    scan->count[i]++;
    age = timespec_age_ms(now, &req->start);
    if (age > scan->oldest_ms[i]) {
        scan->oldest_ms[i] = age;
        scan->oldest_id[i] = req->id;
    }
}

static int should_log(unsigned count, uint64_t oldest_ms, uint64_t threshold_ms,
                      uint64_t now_ms, uint64_t last_log_ms)
{
    return count != 0 && oldest_ms >= threshold_ms &&
           (last_log_ms == 0 || now_ms - last_log_ms >= threshold_ms);
}

int main(void)
{
    const struct timespec now = { .tv_sec = 100, .tv_nsec = 100000000L };
    const struct req_state reqs[] = {
        { 0, 10, { .tv_sec = 30, .tv_nsec = 100000000L } },
        { 0, 11, { .tv_sec = 80, .tv_nsec = 100000000L } },
        { 1, 20, { .tv_sec = 35, .tv_nsec = 100000000L } },
    };
    struct scan_state scan;
    struct conn_state conns[2] = { { 7 }, { 101 } };
    uint64_t last_epoch[2] = { 7, 100 };
    size_t i;

    memset(&scan, 0, sizeof(scan));
    for (i = 0; i < sizeof(reqs) / sizeof(reqs[0]); i++)
        scan_req(&now, &reqs[i], &scan);

    assert(scan.count[0] == 2 && scan.oldest_id[0] == 10 &&
           scan.oldest_ms[0] == 70000);
    assert(scan.count[1] == 1 && scan.oldest_id[1] == 20 &&
           scan.oldest_ms[1] == 65000);
    assert(should_log(scan.count[0], scan.oldest_ms[0], 60000, 100000, 0));
    assert(should_log(scan.count[1], scan.oldest_ms[1], 60000, 100000, 0));

    /* conn0 is stalled even while conn1 continues to make progress. */
    assert((conns[0].progress_epoch != last_epoch[0]) == 0);
    assert((conns[1].progress_epoch != last_epoch[1]) == 1);

    /* A global epoch would incorrectly classify conn0 as progressing. */
    assert(((conns[0].progress_epoch + conns[1].progress_epoch) !=
            (last_epoch[0] + last_epoch[1])) == 1);

    puts("per-connection single-pass stall observer: PASS");
    return 0;
}
