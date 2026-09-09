#define main sshfs_win_program_main
#include "../sshfs-win.c"
#undef main

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_ok(const char *input,
    const char *locuser, const char *remote, const char *port, const char *path)
{
    char buf[256];
    struct instance_parts parts;
    const char *reason = 0;

    assert(strlen(input) < sizeof buf);
    strcpy(buf, input);
    assert(0 == parse_instance_spec(buf, &parts, &reason));
    assert(0 == strcmp(reason, "none"));
    assert((0 == locuser && 0 == parts.locuser) ||
        (0 != locuser && 0 == strcmp(locuser, parts.locuser)));
    assert(0 == strcmp(remote, parts.remote));
    assert((0 == port && 0 == parts.port) ||
        (0 != port && 0 == strcmp(port, parts.port)));
    assert(0 == strcmp(path, parts.path));
}

static void expect_fail(const char *input, const char *expected_reason)
{
    char buf[256];
    struct instance_parts parts;
    const char *reason = 0;

    assert(strlen(input) < sizeof buf);
    strcpy(buf, input);
    assert(0 != parse_instance_spec(buf, &parts, &reason));
    assert(0 == strcmp(expected_reason, reason));
}

int main(void)
{
    unsigned int port = 0;

    assert(0 == parse_port("1", &port) && 1 == port);
    assert(0 == parse_port("65535", &port) && 65535 == port);
    assert(0 != parse_port("", &port));
    assert(0 != parse_port("0", &port));
    assert(0 != parse_port("65536", &port));
    assert(0 != parse_port("22x", &port));
    assert(0 != parse_port("-1", &port));

    expect_ok("user@host", 0, "user@host", 0, "");
    expect_ok("user@host/path", 0, "user@host", 0, "path");
    expect_ok("user@host//path", 0, "user@host", 0, "path");
    expect_ok("user@host////path", 0, "user@host", 0, "path");
    expect_ok("user@host///", 0, "user@host", 0, "");
    expect_ok("DOMAIN+local=user@host!2222/path", "DOMAIN+local",
        "user@host", "2222", "path");
    expect_ok("alias/path!with=separators", 0, "alias", 0,
        "path!with=separators");
    expect_ok("[::1]!22/root", 0, "[::1]", "22", "root");

    expect_fail("", "empty-remote");
    expect_fail("/path", "empty-remote");
    expect_fail("=host", "empty-local-user");
    expect_fail("local=", "empty-remote");
    expect_fail("host!", "empty-port");
    expect_fail("a=b=c", "invalid-instance-separators");
    expect_fail("host!22!23", "invalid-instance-separators");
    expect_fail("host!22=local", "invalid-instance-separators");

    puts("wrapper instance parser: PASS");
    return 0;
}
