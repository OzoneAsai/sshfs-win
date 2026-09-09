#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t *win32_widen_arg(const char *arg)
{
    mbstate_t state;
    const char *src;
    size_t need;
    wchar_t *wide;

    memset(&state, 0, sizeof(state));
    src = arg;
    need = mbsrtowcs(NULL, &src, 0, &state);
    if (need == (size_t)-1)
        return NULL;
    wide = malloc((need + 1) * sizeof(*wide));
    if (!wide)
        return NULL;
    memset(&state, 0, sizeof(state));
    src = arg;
    if (mbsrtowcs(wide, &src, need + 1, &state) == (size_t)-1) {
        free(wide);
        return NULL;
    }
    return wide;
}

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
    *q++ = L'"'; *q = L'\0'; *outp = out;
    return 0;
}

static void check_utf8(const char *s)
{
    wchar_t *wide = win32_widen_arg(s), *quoted = NULL;
    assert(wide != NULL);
    assert(win32_quote_warg(wide, &quoted) == 0);
    assert(quoted != NULL);
    assert(quoted[0] == L'"');
    assert(quoted[wcslen(quoted) - 1] == L'"');
    free(quoted);
    free(wide);
}

int main(void)
{
    const char *loc = setlocale(LC_CTYPE, "");
    if (!loc || strcmp(loc, "C") == 0 || strcmp(loc, "POSIX") == 0)
        loc = setlocale(LC_CTYPE, "C.UTF-8");
    assert(loc != NULL);

    check_utf8("server");
    check_utf8("ユーザー@host:/資料/漢字");
    check_utf8("user@host:/mnt/storage/主文件夹");
    check_utf8("пользователь@сервер:/данные");
    check_utf8("space and \"quote\" \\\\ tail\\");
    puts("windows-commandline-unicode-test: PASS");
    return 0;
}
