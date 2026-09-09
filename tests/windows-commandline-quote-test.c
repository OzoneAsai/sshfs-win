#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static int win32_quote_warg(const wchar_t *arg, wchar_t **outp)
{
    size_t i, n = wcslen(arg), outlen = 2;
    wchar_t *out, *q;
    for (i = 0; i < n;) {
        size_t bs = 0;
        while (i < n && arg[i] == L'\\') { bs++; i++; }
        if (i == n) { outlen += bs * 2; break; }
        if (arg[i] == L'"') outlen += bs * 2 + 2;
        else outlen += bs + 1;
        i++;
    }
    out = malloc((outlen + 1) * sizeof(*out));
    if (!out) return -1;
    q = out; *q++ = L'"';
    for (i = 0; i < n;) {
        size_t bs = 0, j;
        while (i < n && arg[i] == L'\\') { bs++; i++; }
        if (i == n) {
            for (j = 0; j < bs * 2; j++) *q++ = L'\\';
            break;
        }
        if (arg[i] == L'"') {
            for (j = 0; j < bs * 2 + 1; j++) *q++ = L'\\';
            *q++ = L'"';
        } else {
            for (j = 0; j < bs; j++) *q++ = L'\\';
            *q++ = arg[i];
        }
        i++;
    }
    *q++ = L'"'; *q = L'\0'; *outp = out; return 0;
}

static void check(const wchar_t *in, const wchar_t *expected)
{
    wchar_t *out = NULL;
    assert(win32_quote_warg(in, &out) == 0);
    if (wcscmp(out, expected) != 0) {
        fwprintf(stderr, L"input=[%ls]\nactual=[%ls]\nexpected=[%ls]\n", in, out, expected);
        abort();
    }
    free(out);
}

int main(void)
{
    check(L"", L"\"\"");
    check(L"simple", L"\"simple\"");
    check(L"a b", L"\"a b\"");
    check(L"a\\b", L"\"a\\b\"");
    check(L"a\"b", L"\"a\\\"b\"");
    check(L"C:\\path\\", L"\"C:\\path\\\\\"");
    check(L"x\\\\\"y", L"\"x\\\\\\\\\\\"y\"");
    puts("windows-commandline-quote-test: PASS");
    return 0;
}
