#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../windows-commandline-parser.h"

static char *dup_cstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    assert(copy);
    memcpy(copy, s, n);
    return copy;
}

static char *quote_arg(const char *arg)
{
    size_t i, n = strlen(arg), outlen = 2;
    char *out, *q;

    for (i = 0; i < n;) {
        size_t bs = 0;
        while (i < n && arg[i] == '\\') { bs++; i++; }
        if (i == n) { outlen += bs * 2; break; }
        if (arg[i] == '"') outlen += bs * 2 + 2;
        else outlen += bs + 1;
        i++;
    }
    out = malloc(outlen + 1);
    assert(out);
    q = out; *q++ = '"';
    for (i = 0; i < n;) {
        size_t bs = 0, j;
        while (i < n && arg[i] == '\\') { bs++; i++; }
        if (i == n) {
            for (j = 0; j < bs * 2; j++) *q++ = '\\';
            break;
        }
        if (arg[i] == '"') {
            for (j = 0; j < bs * 2 + 1; j++) *q++ = '\\';
            *q++ = '"';
        } else {
            for (j = 0; j < bs; j++) *q++ = '\\';
            *q++ = arg[i];
        }
        i++;
    }
    *q++ = '"'; *q = '\0';
    return out;
}

static void roundtrip(const char **args, int count)
{
    size_t len = 1;
    char *cmd, *p;
    char **quoted = calloc((size_t)count, sizeof *quoted);
    char **parsed = NULL;
    int parsed_count = -1;
    int i;
    assert(quoted);

    for (i = 0; i < count; i++) {
        quoted[i] = quote_arg(args[i]);
        len += strlen(quoted[i]) + 1;
    }
    cmd = malloc(len); assert(cmd);
    p = cmd;
    for (i = 0; i < count; i++) {
        if (i) *p++ = ' ';
        memcpy(p, quoted[i], strlen(quoted[i]));
        p += strlen(quoted[i]);
    }
    *p = '\0';

    assert(0 == win32_parse_command_line_utf8(cmd, &parsed_count, &parsed));
    assert(parsed_count == count);
    for (i = 0; i < count; i++) {
        if (0 != strcmp(parsed[i], args[i])) {
            fprintf(stderr, "arg %d mismatch: [%s] != [%s]\n", i, parsed[i], args[i]);
            abort();
        }
    }

    win32_free_parsed_argv(parsed);
    for (i = 0; i < count; i++) free(quoted[i]);
    free(quoted); free(cmd);
}


static unsigned rng_state = 0x5a17c3d1U;
static unsigned rnd(void)
{
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state;
}

static char *random_arg(void)
{
    static const char *atoms[] = {
        "a", "Z", "0", " ", "\\", "\"", "-", "_", ".", ":",
        "漢", "字", "主", "ф", "я", "é"
    };
    char *s = calloc(1, 256);
    size_t len = 0;
    unsigned count = rnd() % 20U;
    unsigned i;
    assert(s);
    for (i = 0; i < count; i++) {
        const char *a = atoms[rnd() % (sizeof atoms / sizeof atoms[0])];
        size_t n = strlen(a);
        assert(len + n < 255);
        memcpy(s + len, a, n);
        len += n;
    }
    return s;
}

static void property_roundtrips(void)
{
    int run;
    for (run = 0; run < 2000; run++) {
        int count = 1 + (int)(rnd() % 8U), i;
        char **owned = calloc((size_t)count, sizeof *owned);
        const char **args = calloc((size_t)count, sizeof *args);
        assert(owned && args);
        for (i = 0; i < count; i++) {
            owned[i] = random_arg();
            args[i] = owned[i];
        }
        /* argv[0] must be non-empty for a process command line. */
        if ('\0' == owned[0][0]) {
            free(owned[0]);
            owned[0] = dup_cstr("sshfs-win.exe");
            assert(owned[0]);
            args[0] = owned[0];
        }
        roundtrip(args, count);
        for (i = 0; i < count; i++) free(owned[i]);
        free(args); free(owned);
    }
}

static void parse_exact(const char *cmd, const char **expected, int count)
{
    char **parsed = NULL;
    int argc = -1, i;
    assert(0 == win32_parse_command_line_utf8(cmd, &argc, &parsed));
    assert(argc == count);
    for (i = 0; i < count; i++)
        assert(0 == strcmp(parsed[i], expected[i]));
    win32_free_parsed_argv(parsed);
}

int main(void)
{
    const char *mixed[] = {"C:\\Program Files\\SSHFS-Win\\bin\\sshfs-win.exe", "svc", "\\sshfs.k\\user@host", "Z:"};
    parse_exact("\"C:\\Program Files\\SSHFS-Win\\bin\\sshfs-win.exe\" svc \"\\sshfs.k\\user@host\" \"Z:\"", mixed, 4);
    {
        const char *plain[] = {"abc", "def", "ghi"};
        parse_exact("abc def\tghi", plain, 3);
    }
    {
        /* Microsoft CRT rule: a quote pair inside a quoted span is literal. */
        const char *quote_pair[] = {"prog", "ab\" c d"};
        parse_exact("prog a\"b\"\" c d", quote_pair, 2);
    }
    {
        /*
         * Backslashes do not escape unquoted whitespace.  Re-check the
         * delimiter after the slash run or the next argument is swallowed.
         */
        const char *slash_space[] = {"prog", "abc\\", "def"};
        parse_exact("prog abc\\ def", slash_space, 3);
    }
    {
        const char *unc_trailing_slash[] = {
            "sshfs-win.exe", "svc", "\\sshfs.k\\user@host\\", "Z:"
        };
        parse_exact("sshfs-win.exe svc \\sshfs.k\\user@host\\ Z:",
                    unc_trailing_slash, 4);
    }
    const char *a1[] = {"sshfs-win.exe", "svc", "\\sshfs.kr\\wutianyu@192.168.3.2\\mnt\\storage\\主文件夹", "Z:", "DOMAIN\\ユーザー"};
    const char *a2[] = {"sshfs-win.exe", "svc", "\\sshfs\\пользователь@сервер!2222\\данные", "X:", "DOMAIN\\пользователь"};
    const char *a3[] = {"C:\\Program Files\\SSHFS-Win\\bin\\sshfs-win.exe", "svc", "\\sshfs.k\\user@host\\path with spaces\\", "Y:", "a\\b\"c"};
    const char *a4[] = {"x", "", "simple", "trailing\\", "x\\\\\"y"};
    roundtrip(a1, 5);
    roundtrip(a2, 5);
    roundtrip(a3, 5);
    roundtrip(a4, 5);
    property_roundtrips();
    puts("windows-native-argv-test: PASS (production parser, Microsoft corpus + 2000 property round-trips)");
    return 0;
}
