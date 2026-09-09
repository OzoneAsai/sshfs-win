#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_vsock_address(const char *vsock, unsigned int *cid,
                               unsigned int *port)
{
    const char *delim;
    const char *p;
    char *end;
    unsigned long value;

    if (vsock == NULL || cid == NULL || port == NULL)
        return -1;
    delim = strchr(vsock, ':');
    if (delim == NULL || delim == vsock || delim[1] == '\0')
        return -1;
    for (p = vsock; p < delim; p++)
        if (*p < '0' || *p > '9')
            return -1;
    for (p = delim + 1; *p != '\0'; p++)
        if (*p < '0' || *p > '9')
            return -1;

    errno = 0;
    value = strtoul(vsock, &end, 10);
    if (errno != 0 || end != delim || value > UINT_MAX)
        return -1;
    *cid = (unsigned int)value;

    errno = 0;
    value = strtoul(delim + 1, &end, 10);
    if (errno != 0 || *end != '\0' || value > UINT_MAX)
        return -1;
    *port = (unsigned int)value;
    return 0;
}

int main(void)
{
    char reconnect_input[] = "3:1024";
    const char original[] = "3:1024";
    unsigned int cid, port;

    assert(parse_vsock_address(reconnect_input, &cid, &port) == 0);
    assert(cid == 3 && port == 1024);
    assert(strcmp(reconnect_input, original) == 0);

    /* Reconnect must be able to parse the same persistent option again. */
    assert(parse_vsock_address(reconnect_input, &cid, &port) == 0);
    assert(strcmp(reconnect_input, original) == 0);

    assert(parse_vsock_address("", &cid, &port) == -1);
    assert(parse_vsock_address(":22", &cid, &port) == -1);
    assert(parse_vsock_address("3:", &cid, &port) == -1);
    assert(parse_vsock_address("3", &cid, &port) == -1);
    assert(parse_vsock_address("3:22junk", &cid, &port) == -1);
    assert(parse_vsock_address("cid:22", &cid, &port) == -1);
    assert(parse_vsock_address("+3:22", &cid, &port) == -1);
    assert(parse_vsock_address("-1:22", &cid, &port) == -1);
    assert(parse_vsock_address("3:-1", &cid, &port) == -1);
    assert(parse_vsock_address("3:22:44", &cid, &port) == -1);

#if ULONG_MAX > UINT_MAX
    assert(parse_vsock_address("4294967296:22", &cid, &port) == -1);
    assert(parse_vsock_address("3:4294967296", &cid, &port) == -1);
#endif

    puts("vsock-parser-contract-test: PASS");
    return 0;
}
