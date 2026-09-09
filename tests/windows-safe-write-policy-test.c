#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

static int sync_write_policy(uint32_t raw)
{
    if (raw == 0 || raw == 1)
        return (int)raw;
    return 1;
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
    must(sync_write_policy(0) == 0, "explicit opt-out is honored");
    must(sync_write_policy(1) == 1, "explicit safe-writes is honored");
    must(sync_write_policy(UINT32_MAX) == 1, "missing registry value fails safe");
    must(sync_write_policy(2) == 1, "invalid low value fails safe");
    must(sync_write_policy(0x12345678u) == 1, "invalid arbitrary value fails safe");
    puts("windows safe-write policy: PASS");
    return 0;
}
