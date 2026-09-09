#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

struct file_state {
    pthread_cond_t write_finished;
    int write_finished_initialized;
};

static unsigned init_calls;
static unsigned destroy_calls;
static int fail_next_init;

static int tracked_cond_init(pthread_cond_t *cond)
{
    init_calls++;
    if (fail_next_init) {
        fail_next_init = 0;
        return 12; /* model ENOMEM without depending on errno values */
    }
    return pthread_cond_init(cond, NULL);
}

static void tracked_cond_destroy(pthread_cond_t *cond)
{
    int rc = pthread_cond_destroy(cond);
    assert(rc == 0);
    destroy_calls++;
}

static int file_open_state(struct file_state *sf, int sync_write, int flags)
{
    memset(sf, 0, sizeof(*sf));
    if (!sync_write && (flags & O_ACCMODE) != O_RDONLY) {
        int err = tracked_cond_init(&sf->write_finished);
        if (err)
            return -err;
        sf->write_finished_initialized = 1;
    }
    return 0;
}

static void file_open_failure_cleanup(struct file_state *sf)
{
    if (sf->write_finished_initialized) {
        tracked_cond_destroy(&sf->write_finished);
        sf->write_finished_initialized = 0;
    }
}

static void file_release(struct file_state *sf)
{
    if (sf->write_finished_initialized) {
        tracked_cond_destroy(&sf->write_finished);
        sf->write_finished_initialized = 0;
    }
}

int main(void)
{
    struct file_state sf;
    unsigned init0, destroy0;

    /* Read-only async-write mode: no write condition exists. */
    init0 = init_calls;
    destroy0 = destroy_calls;
    assert(file_open_state(&sf, 0, O_RDONLY) == 0);
    assert(!sf.write_finished_initialized);
    file_release(&sf);
    assert(init_calls == init0);
    assert(destroy_calls == destroy0);

    /* O_WRONLY and O_RDWR need exactly one condition in async-write mode. */
    assert(file_open_state(&sf, 0, O_WRONLY) == 0);
    assert(sf.write_finished_initialized);
    file_release(&sf);
    assert(init_calls == init0 + 1);
    assert(destroy_calls == destroy0 + 1);

    assert(file_open_state(&sf, 0, O_RDWR) == 0);
    assert(sf.write_finished_initialized);
    file_release(&sf);
    assert(init_calls == init0 + 2);
    assert(destroy_calls == destroy0 + 2);

    /* sync_write never queues async writes, independent of access mode. */
    init0 = init_calls;
    destroy0 = destroy_calls;
    assert(file_open_state(&sf, 1, O_WRONLY) == 0);
    assert(!sf.write_finished_initialized);
    file_release(&sf);
    assert(init_calls == init0);
    assert(destroy_calls == destroy0);

    assert(file_open_state(&sf, 1, O_RDWR) == 0);
    assert(!sf.write_finished_initialized);
    file_release(&sf);
    assert(init_calls == init0);
    assert(destroy_calls == destroy0);

    /* If cond init itself fails, ownership was never acquired. */
    fail_next_init = 1;
    init0 = init_calls;
    destroy0 = destroy_calls;
    assert(file_open_state(&sf, 0, O_WRONLY) < 0);
    assert(!sf.write_finished_initialized);
    file_open_failure_cleanup(&sf);
    assert(init_calls == init0 + 1);
    assert(destroy_calls == destroy0);

    /* Later OPEN failure after successful cond init destroys exactly once. */
    init0 = init_calls;
    destroy0 = destroy_calls;
    assert(file_open_state(&sf, 0, O_RDWR) == 0);
    file_open_failure_cleanup(&sf);
    assert(init_calls == init0 + 1);
    assert(destroy_calls == destroy0 + 1);

    /* Model a Git/indexer scan: 100k read-only opens => zero condvars. */
    init0 = init_calls;
    destroy0 = destroy_calls;
    for (unsigned i = 0; i < 100000; i++) {
        assert(file_open_state(&sf, 0, O_RDONLY) == 0);
        file_release(&sf);
    }
    assert(init_calls == init0);
    assert(destroy_calls == destroy0);

    puts("lazy-write-condvar lifecycle PASS");
    return 0;
}
