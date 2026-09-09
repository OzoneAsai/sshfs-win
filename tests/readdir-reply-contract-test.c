#include <assert.h>
#include <errno.h>
#include <stdio.h>

/* buf_get_entries() must expose malformed wire encoding as an I/O/protocol
 * failure, never the raw parser sentinel -1 (which FUSE would interpret as
 * -EPERM).  Mapping/id-policy errors such as -EPERM remain semantic errors. */
static int entry_parse_result(int name_ok, int longname_ok, int attrs_result)
{
    if (!name_ok)
        return -EIO;
    if (!longname_ok)
        return -EIO;
    return attrs_result;
}

int main(void)
{
    assert(entry_parse_result(1, 1, 0) == 0);
    assert(entry_parse_result(0, 1, 0) == -EIO);
    assert(entry_parse_result(1, 0, 0) == -EIO);
    assert(entry_parse_result(1, 1, -EIO) == -EIO);
    assert(entry_parse_result(1, 1, -EPERM) == -EPERM);

    /* Regression target: raw parser failure must never escape as -1. */
    assert(entry_parse_result(1, 0, 0) != -1);

    puts("readdir reply parse contract: PASS");
    return 0;
}
