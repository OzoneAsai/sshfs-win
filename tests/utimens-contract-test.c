#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

struct resolved_times {
    int noop;
    int err;
    uint32_t atime;
    uint32_t mtime;
};

static struct resolved_times resolve_times(const struct timespec tv[2],
                                           time_t current_atime,
                                           time_t current_mtime,
                                           time_t now)
{
    struct resolved_times out = {0, 0, 0, 0};
    int omit_a = tv != NULL && tv[0].tv_nsec == UTIME_OMIT;
    int omit_m = tv != NULL && tv[1].tv_nsec == UTIME_OMIT;
    int now_a = tv == NULL || tv[0].tv_nsec == UTIME_NOW;
    int now_m = tv == NULL || tv[1].tv_nsec == UTIME_NOW;
    time_t a, m;

    if (omit_a && omit_m) {
        out.noop = 1;
        return out;
    }
    if (tv != NULL) {
        if (!omit_a && !now_a && (tv[0].tv_nsec < 0 || tv[0].tv_nsec >= 1000000000L)) {
            out.err = -EINVAL;
            return out;
        }
        if (!omit_m && !now_m && (tv[1].tv_nsec < 0 || tv[1].tv_nsec >= 1000000000L)) {
            out.err = -EINVAL;
            return out;
        }
    }

    a = omit_a ? current_atime : (now_a ? now : tv[0].tv_sec);
    m = omit_m ? current_mtime : (now_m ? now : tv[1].tv_sec);
    if (a < 0 || m < 0 || (uintmax_t)a > UINT32_MAX || (uintmax_t)m > UINT32_MAX) {
        out.err = -ERANGE;
        return out;
    }
    out.atime = (uint32_t)a;
    out.mtime = (uint32_t)m;
    return out;
}

int main(void)
{
    struct timespec tv[2];
    struct resolved_times r;

    /* Unix epoch is a real timestamp, not shorthand for "now". */
    tv[0] = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 };
    tv[1] = (struct timespec){ .tv_sec = 1, .tv_nsec = 0 };
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.err == 0 && !r.noop);
    assert(r.atime == 0 && r.mtime == 1);

    tv[0].tv_nsec = UTIME_NOW;
    tv[1] = (struct timespec){ .tv_sec = 7, .tv_nsec = 0 };
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.atime == 1234 && r.mtime == 7);

    tv[0].tv_nsec = UTIME_OMIT;
    tv[1].tv_sec = 8;
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.atime == 10 && r.mtime == 8);

    tv[0].tv_nsec = UTIME_OMIT;
    tv[1].tv_nsec = UTIME_OMIT;
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.noop && r.err == 0);

    r = resolve_times(NULL, 10, 20, 1234);
    assert(r.atime == 1234 && r.mtime == 1234);

    tv[0] = (struct timespec){ .tv_sec = -1, .tv_nsec = 0 };
    tv[1] = (struct timespec){ .tv_sec = 1, .tv_nsec = 0 };
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.err == -ERANGE);

    tv[0] = (struct timespec){ .tv_sec = 1, .tv_nsec = 1000000000L };
    r = resolve_times(tv, 10, 20, 1234);
    assert(r.err == -EINVAL);

    puts("utimens-contract-test: PASS");
    return 0;
}
