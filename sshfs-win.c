/**
 * sshfs-win.c
 *
 * Copyright 2015-2021 Bill Zissimopoulos
 */
/*
 * This file is part of SSHFS-Win.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "portable-format.h"

#ifdef __CYGWIN__
#include <windows.h>
#endif

static char *sshfs = "/usr/bin/sshfs.exe";


static const char *env_or_unset(const char *name)
{
    const char *value = getenv(name);
    return 0 != value && '\0' != value[0] ? value : "<unset>";
}

static void diag(const char *stage, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "sshfs-win: stage=%s pid=%ld uid=%ld euid=%ld",
        stage, (long)getpid(), (long)getuid(), (long)geteuid());
    if (0 != fmt && '\0' != *fmt)
    {
        fputc(' ', stderr);
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fputc('\n', stderr);
    fflush(stderr);
}

#if 0
#define execve pr_execv
static void pr_execv(const char *path, char *argv[], ...)
{
    fprintf(stderr, "%s\n", path);
    for (; 0 != *argv; argv++)
        fprintf(stderr, "    %s\n", *argv);
}
#endif


#ifdef __CYGWIN__
/*
 * Parse a Windows CRT-style command line that has already been converted to
 * UTF-8. Quote and backslash are ASCII, so parsing after UTF-16 -> UTF-8
 * conversion preserves Windows quoting semantics while keeping the parser
 * portable and unit-testable.
 */
static int win32_parse_command_line_utf8(const char *command_line,
    int *argc_out, char ***argv_out)
{
    size_t n;
    char *storage, *src, *dst;
    char **argv;
    int argc = 0;

    if (0 == command_line || 0 == argc_out || 0 == argv_out)
        return -1;

    n = strlen(command_line);
    storage = malloc(n + 1);
    argv = calloc(n + 2, sizeof *argv);
    if (0 == storage || 0 == argv)
    {
        free(argv);
        free(storage);
        return -1;
    }
    memcpy(storage, command_line, n + 1);

    src = storage;
    dst = storage;
    while (*src)
    {
        int quoted = 0;

        while (' ' == *src || '\t' == *src)
            src++;
        if ('\0' == *src)
            break;

        argv[argc++] = dst;
        while (*src)
        {
            size_t backslashes = 0, i;

            if (!quoted && (' ' == *src || '\t' == *src))
                break;

            while ('\\' == *src)
            {
                backslashes++;
                src++;
            }

            if ('"' == *src)
            {
                for (i = 0; i < backslashes / 2; i++)
                    *dst++ = '\\';
                if (0 == backslashes && quoted && '"' == src[1])
                {
                    *dst++ = '"';
                    src += 2;
                }
                else if (backslashes & 1)
                {
                    *dst++ = '"';
                    src++;
                }
                else
                {
                    quoted = !quoted;
                    src++;
                }
                continue;
            }

            for (i = 0; i < backslashes; i++)
                *dst++ = '\\';

            if ('\0' == *src)
                break;
            /*
             * Backslash has no escaping role for unquoted whitespace in the
             * Microsoft CRT grammar.  Re-check the delimiter after consuming
             * a backslash run so an argument ending in '\\' does not absorb
             * the separator and the following argument.
             */
            if (!quoted && (' ' == *src || '\t' == *src))
                break;
            *dst++ = *src++;
        }
        while (' ' == *src || '\t' == *src)
            src++;
        *dst++ = '\0';
    }
    argv[argc] = 0;
    if (0 == argc)
    {
        free(argv);
        free(storage);
        return -1;
    }

    *argc_out = argc;
    *argv_out = argv;
    return 0;
}

static void win32_free_parsed_argv(char **argv)
{
    if (0 != argv)
    {
        if (0 != argv[0])
            free(argv[0]);
        free(argv);
    }
}

typedef LPWSTR (WINAPI *raw_get_command_line_w_fn)(void);

static int win32_raw_utf8_argv(int *argc_out, char ***argv_out,
    const char **failure_stage, DWORD *failure_error)
{
    HMODULE kernel32;
    raw_get_command_line_w_fn raw_get_command_line_w;
    LPWSTR wide;
    char *utf8;
    int size, result;

    if (0 != failure_stage)
        *failure_stage = "none";
    if (0 != failure_error)
        *failure_error = ERROR_SUCCESS;

#define NATIVE_ARGV_FAIL(stage_, error_) \
    do { \
        if (0 != failure_stage) \
            *failure_stage = (stage_); \
        if (0 != failure_error) \
            *failure_error = (error_); \
        return -1; \
    } while (0)

    kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (0 == kernel32)
        NATIVE_ARGV_FAIL("kernel32-module", GetLastError());

    /*
     * Cygwin exports its own GetCommandLineW that reconstructs from Cygwin's
     * argv. Resolve the kernel32 export dynamically so this reads the original
     * UTF-16 process command line supplied by WinFsp Launcher.
     */
    raw_get_command_line_w = (raw_get_command_line_w_fn)(uintptr_t)
        GetProcAddress(kernel32, "GetCommandLineW");
    if (0 == raw_get_command_line_w)
        NATIVE_ARGV_FAIL("get-command-line-export", GetLastError());

    wide = raw_get_command_line_w();
    if (0 == wide)
        NATIVE_ARGV_FAIL("get-command-line", ERROR_INVALID_DATA);

    size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide, -1, 0, 0, 0, 0);
    if (0 == size)
        NATIVE_ARGV_FAIL("utf16-size", GetLastError());

    utf8 = malloc((size_t)size);
    if (0 == utf8)
        NATIVE_ARGV_FAIL("utf8-allocation", ERROR_NOT_ENOUGH_MEMORY);

    if (0 == WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide, -1, utf8, size, 0, 0))
    {
        DWORD error = GetLastError();
        free(utf8);
        NATIVE_ARGV_FAIL("utf16-conversion", error);
    }

    result = win32_parse_command_line_utf8(utf8, argc_out, argv_out);
    free(utf8);
    if (0 != result)
        NATIVE_ARGV_FAIL("command-line-parse", ERROR_INVALID_DATA);

#undef NATIVE_ARGV_FAIL
    return 0;
}

static void normalize_launcher_argv(int *argc, char ***argv)
{
    int native_argc;
    int cygwin_argc;
    char **native_argv = 0;
    const char *failure_stage = "none";
    DWORD failure_error = ERROR_SUCCESS;

    if (0 == argc || 0 == argv || 2 > *argc ||
        0 != strcmp((*argv)[1], "svc"))
        return;

    cygwin_argc = *argc;
    if (0 != win32_raw_utf8_argv(&native_argc, &native_argv,
        &failure_stage, &failure_error))
    {
        diag("argv.native",
            "status=unavailable stage=%s win32-error=%lu fallback=cygwin",
            failure_stage, (unsigned long)failure_error);
        return;
    }

    if (native_argc != cygwin_argc)
    {
        diag("argv.native",
            "status=active count-mismatch native=%d cygwin=%d authority=native encoding=utf8",
            native_argc, cygwin_argc);
    }
    else
    {
        diag("argv.native",
            "status=active argc=%d count=match authority=native encoding=utf8",
            native_argc);
    }

    *argc = native_argc;
    *argv = native_argv;
}
#else
static void normalize_launcher_argv(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
}
#endif

static void usage(void)
{
    fprintf(stderr,
        "usage: sshfs-win cmd SSHFS_COMMAND_LINE\n"
        "    SSHFS_COMMAND_LINE  command line to pass to sshfs\n"
        "\n"
        "usage: sshfs-win svc PREFIX X: [LOCUSER] [SSHFS_OPTIONS]\n"
        "    PREFIX              Windows UNC prefix (note single backslash)\n"
        "                        \\sshfs[.SUFFIX]\\[LOCUSER=]REMUSER@HOST[!PORT][\\PATH]\n"
        "                        \\sshfs[.SUFFIX]\\alias[\\PATH]\n"
        "                        sshfs: remote user home dir\n"
        "                        sshfs.r: remote root dir\n"
        "                        sshfs.k: remote user home dir with key authentication\n"
        "                        sshfs.kr: remote root dir with key authentication\n"
        "    LOCUSER             local user (DOMAIN+USERNAME)\n"
        "    REMUSER             remote user\n"
        "    HOST                remote host\n"
        "    PORT                remote port\n"
        "    PATH                remote path (relative to remote home or root)\n"
        "    X:                  mount drive\n"
        "    SSHFS_OPTIONS       additional options to pass to SSHFS\n");
    exit(2);
}

static void concat_argv(char *dst[], char *src[])
{
    for (; 0 != *dst; dst++)
        ;
    for (; 0 != (*dst = *src); dst++, src++)
        ;
}

static int parse_port(const char *text, unsigned int *value)
{
    char *end;
    unsigned long port;

    if (0 == text || '\0' == *text)
        return -1;

    errno = 0;
    port = strtoul(text, &end, 10);
    if (0 != errno || '\0' != *end || 0 == port || 65535 < port)
        return -1;

    *value = (unsigned int)port;
    return 0;
}

struct instance_parts {
    char *locuser;
    char *remote;
    char *port;
    char *path;
};

static int parse_instance_spec(char *spec, struct instance_parts *parts,
    const char **reason)
{
    char *p;
    int seen_local_separator = 0;
    int seen_port_separator = 0;

    if (0 != reason)
        *reason = "invalid-instance";
    if (0 == spec || 0 == parts)
        return -1;

    parts->locuser = 0;
    parts->remote = spec;
    parts->port = 0;
    parts->path = spec + strlen(spec);

    p = spec;
    while (*p && '/' != *p)
    {
        if ('=' == *p)
        {
            if (seen_local_separator || seen_port_separator)
            {
                if (0 != reason)
                    *reason = "invalid-instance-separators";
                return -1;
            }
            seen_local_separator = 1;
            *p = '\0';
            parts->locuser = parts->remote;
            parts->remote = p + 1;
        }
        else if ('!' == *p)
        {
            if (seen_port_separator)
            {
                if (0 != reason)
                    *reason = "invalid-instance-separators";
                return -1;
            }
            seen_port_separator = 1;
            *p = '\0';
            parts->port = p + 1;
        }
        p++;
    }

    if (*p)
    {
        *p++ = '\0';
        /*
         * Treat repeated UNC separators as one boundary.  Leaving a leading
         * slash here would turn a home-relative class into an absolute SFTP
         * path when do_svc() builds REMOTE:PATH. Root semantics belong to the
         * explicit .r/.kr launcher classes, not separator count.
         */
        while ('/' == *p)
            p++;
        parts->path = p;
    }

    if ('\0' == parts->remote[0])
    {
        if (0 != reason)
            *reason = "empty-remote";
        return -1;
    }
    if (0 != parts->locuser && '\0' == parts->locuser[0])
    {
        if (0 != reason)
            *reason = "empty-local-user";
        return -1;
    }
    if (0 != parts->port && '\0' == parts->port[0])
    {
        if (0 != reason)
            *reason = "empty-port";
        return -1;
    }

    if (0 != reason)
        *reason = "none";
    return 0;
}

enum reg_read_status {
    REG_READ_ERROR = -1,
    REG_READ_MISSING = 0,
    REG_READ_VALUE = 1,
};

static int reg_get_dword(const char *cls, const char *name, uint32_t *value)
{
    char *regpath = 0;
    int regfd = -1;
    uint32_t raw = 0;
    unsigned char *p = (unsigned char *)&raw;
    size_t used = 0;
    int saved_errno;

    if (0 == cls || '\0' == *cls || 0 == name || '\0' == *name || 0 == value)
    {
        errno = EINVAL;
        diag("registry.failure", "name=%s reason=invalid-argument",
            0 != name ? name : "<null>");
        return REG_READ_ERROR;
    }

    if (-1 == sshfs_win_alloc_printf(&regpath,
        "/proc/registry32/HKEY_LOCAL_MACHINE/Software/WinFsp/Services/%s/%s",
        cls, name))
    {
        errno = ENOMEM;
        diag("registry.failure", "class=%s name=%s reason=allocation-failed",
            cls, name);
        return REG_READ_ERROR;
    }

    regfd = open(regpath, O_RDONLY);
    if (-1 == regfd)
    {
        saved_errno = errno;
        free(regpath);
        if (ENOENT == saved_errno)
            return REG_READ_MISSING;

        errno = saved_errno;
        diag("registry.failure", "class=%s name=%s operation=open errno=%d error=%s",
            cls, name, saved_errno, strerror(saved_errno));
        return REG_READ_ERROR;
    }

    while (used < sizeof raw)
    {
        ssize_t n = read(regfd, p + used, sizeof raw - used);
        if (0 < n)
        {
            used += (size_t)n;
            continue;
        }
        if (-1 == n && EINTR == errno)
            continue;

        saved_errno = 0 == n ? EIO : errno;
        if (-1 == close(regfd))
            diag("registry.cleanup", "class=%s name=%s operation=close errno=%d error=%s",
                cls, name, errno, strerror(errno));
        free(regpath);
        errno = saved_errno;
        diag("registry.failure",
            "class=%s name=%s operation=read bytes=%zu expected=%zu errno=%d error=%s",
            cls, name, used, sizeof raw, saved_errno, strerror(saved_errno));
        return REG_READ_ERROR;
    }

    if (-1 == close(regfd))
        diag("registry.cleanup", "class=%s name=%s operation=close errno=%d error=%s",
            cls, name, errno, strerror(errno));

    free(regpath);
    *value = raw;
    return REG_READ_VALUE;
}

static int reg_bool(const char *cls, const char *name, int default_value,
    int *value)
{
    uint32_t raw;
    int status = reg_get_dword(cls, name, &raw);

    if (REG_READ_ERROR == status)
        return -1;
    if (REG_READ_MISSING == status)
    {
        *value = default_value;
        return 0;
    }
    if (0 != raw && 1 != raw)
    {
        diag("svc.config", "name=%s status=invalid value=%u expected=0-or-1",
            name, raw);
        return -1;
    }

    *value = (int)raw;
    return 0;
}

static int reg_uint(const char *cls, const char *name, unsigned int default_value,
    unsigned int minimum, unsigned int maximum, int allow_zero,
    unsigned int *value)
{
    uint32_t raw;
    int status = reg_get_dword(cls, name, &raw);

    if (REG_READ_ERROR == status)
        return -1;
    if (REG_READ_MISSING == status)
    {
        *value = default_value;
        return 0;
    }
    if ((0 == raw && allow_zero) ||
        (minimum <= raw && raw <= maximum))
    {
        *value = (unsigned int)raw;
        return 0;
    }

    diag("svc.config",
        "name=%s status=invalid value=%u expected=%s%u..%u",
        name, raw, allow_zero ? "0-or-" : "", minimum, maximum);
    return -1;
}

static int fixenv_and_execv(const char *path, char **argv)
{
    extern char **environ;
    char **oldenv, **oldp;
    char **newenv, **newp;
    size_t len, siz;
    int res;

    oldenv = environ;

    siz = 0;
    for (oldp = oldenv; *oldp; oldp++)
    {
        if (0 == strncmp(*oldp, "PATH=", 5))
        {
            len = strlen(*oldp + 5);
            siz += len + sizeof "PATH=/usr/bin:";
        }
        else
        {
            len = strlen(*oldp);
            siz += len + 1;
        }
    }
    oldp++;

    newenv = malloc((oldp - oldenv) * sizeof(char *) + siz);
    if (0 != newenv)
    {
        siz = (oldp - oldenv) * sizeof(char *);
        for (oldp = oldenv, newp = newenv; *oldp; oldp++, newp++)
        {
            *newp = (char *)newenv + siz;
            if (0 == strncmp(*oldp, "PATH=", 5))
            {
                len = strlen(*oldp + 5);
                siz += len + sizeof "PATH=/usr/bin:";
                memcpy(*newp, "PATH=/usr/bin:", sizeof "PATH=/usr/bin:" - 1);
                memcpy(*newp + sizeof "PATH=/usr/bin:" - 1, *oldp + 5, len + 1);
            }
            else
            {
                len = strlen(*oldp);
                siz += len + 1;
                memcpy(*newp, *oldp, len + 1);
            }
        }
        *newp = 0;
    }
    else
    {
        /*
         * Preserve the caller's identity/configuration environment on OOM.
         * The executable and ssh_command paths are absolute, so dropping
         * HOME/USERPROFILE here is more harmful than leaving PATH unchanged.
         */
        diag("env.fallback", "reason=allocation-failed action=preserve-original");
        newenv = oldenv;
        newp = 0;
    }

    diag("exec.attempt", "path=%s HOME=%s USERPROFILE=%s",
        path, env_or_unset("HOME"), env_or_unset("USERPROFILE"));

#if 1
    res = execve(path, argv, newenv);
    diag("exec.failure", "path=%s errno=%d error=%s",
        path, errno, strerror(errno));
#else
    char **p;
    for (p = newenv; *p; p++)
        printf("%s\n", *p);
    assert(newp == p);
#endif

    if (newenv != oldenv)
        free(newenv);

    return res;
}

static int do_cmd(int argc, char *argv[])
{
    diag("cmd.entry", "argc=%d", argc);

    if (200 < argc)
        usage();

    char *sshfs_argv[256] =
    {
        sshfs, 0,
    };

    concat_argv(sshfs_argv, argv + 1);

    if (-1 == fixenv_and_execv(sshfs, sshfs_argv))
        return ENOENT == errno ? 127 : 126;
    return 1;
}

static int do_svc(int argc, char *argv[])
{
    diag("svc.entry", "argc=%d", argc);

#define SSHFS_ARGS                      \
    "-f",                               \
    "-orellinks",                       \
    "-ofstypename=SSHFS",               \
    "-ossh_command=/usr/bin/ssh.exe"

    if (3 > argc || 200 < argc)
        usage();

    struct passwd *passwd = 0;
    int rootdir, credentials, reconnect, delayconnect, syncwrite;
    unsigned int portnum, stallwarn, handlewarn, handlelimit, maxconns;
    char idmap[64], portopt[32];
    char stallopt[64], handleopt[64], limitopt[64], maxconnopt[64];
    char *volpfx = 0, *remote = 0;
    char *authmeth;
    char *cls, *locuser, *locuser_nodom, *userhost, *port, *root, *path, *p;
    struct instance_parts instance;
    const char *instance_error = "none";

    if (-1 == sshfs_win_alloc_printf(&volpfx, "--VolumePrefix=%s", argv[1]))
    {
        diag("allocation.failure", "object=volume-prefix");
        return 1;
    }

    /* translate backslash to forward slash */
    for (p = argv[1]; *p; p++)
        if ('\\' == *p)
            *p = '/';

    /* skip class name */
    p = argv[1];
    while ('/' == *p)
        p++;
    cls = p;
    while (*p && '/' != *p)
        p++;
    if (*p)
        *p++ = '\0';
    while ('/' == *p)
        p++;
    if (0 != reg_bool(cls, "sshfs.rootdir", 0, &rootdir) ||
        0 != reg_bool(cls, "sshfs.reconnect", 0, &reconnect) ||
        0 != reg_bool(cls, "Credentials", 0, &credentials))
    {
        fprintf(stderr, "sshfs-win: cannot read launcher configuration for class %s\n",
            cls);
        free(volpfx);
        return 1;
    }

    {
        int delaydefault = credentials ? 0 : 1;
        if (0 != reg_bool(cls, "sshfs.delay_connect", delaydefault, &delayconnect))
        {
            free(volpfx);
            return 1;
        }
        /*
         * WinFsp's credential bridge waits for password_stdout's initial
         * status, which is emitted only after the SFTP handshake. Delaying
         * that handshake would deadlock the launcher and network provider.
         */
        if (credentials && delayconnect)
        {
            diag("svc.config",
                "name=sshfs.delay_connect value=1 status=incompatible-with-password-bridge");
            fprintf(stderr,
                "sshfs-win: sshfs.delay_connect=1 is invalid for password launcher classes\n");
            free(volpfx);
            return 1;
        }
    }

    if (0 != reg_bool(cls, "sshfs.sync_write", 1, &syncwrite) ||
        0 != reg_uint(cls, "sshfs.stall_warn", 0, 5, 3600, 1, &stallwarn) ||
        0 != reg_uint(cls, "sshfs.handle_warn", 0, 16, 65535, 1, &handlewarn) ||
        0 != reg_uint(cls, "sshfs.handle_limit", 0, 32, 65535, 1, &handlelimit) ||
        0 != reg_uint(cls, "sshfs.max_conns", 1, 1, 16, 0, &maxconns))
    {
        fprintf(stderr, "sshfs-win: invalid launcher configuration for class %s\n",
            cls);
        free(volpfx);
        return 1;
    }

    root = rootdir ? "/" : "";
    diag("svc.class",
        "class=%s rootdir=%s reconnect=%s delay_connect=%s sync_write=%s stall_warn=%u handle_warn=%u handle_limit=%u max_conns=%u", cls,
        rootdir ? "yes" : "no", reconnect ? "yes" : "no",
        delayconnect ? "yes" : "no", syncwrite ? "yes" : "no",
        stallwarn, handlewarn, handlelimit, maxconns);

    /* parse instance name (syntax: [locuser=]user@host[!port]) */
    locuser_nodom = 0;
    if (0 != parse_instance_spec(p, &instance, &instance_error))
    {
        diag("argument.error", "reason=%s", instance_error);
        fprintf(stderr,
            "sshfs-win: malformed instance; expected [LOCUSER=]REMOTE[!PORT][/PATH]\n");
        free(volpfx);
        return 2;
    }
    locuser = instance.locuser;
    userhost = instance.remote;
    port = instance.port;
    path = instance.path;

    if (port != 0)
    {
        if (0 != parse_port(port, &portnum))
        {
            diag("argument.error", "reason=invalid-port");
            fprintf(stderr, "sshfs-win: invalid port; expected 1..65535\n");
            free(volpfx);
            return 2;
        }
        snprintf(portopt, sizeof portopt, "-oPort=%u", portnum);
    }
    else
    {
        portopt[0] = '\0';
    }
    if (-1 == sshfs_win_alloc_printf(&remote, "%s:%s%s", userhost, root, path))
    {
        diag("allocation.failure", "object=remote-spec");
        free(volpfx);
        return 1;
    }
    diag("svc.remote", "port=%s remote_path=%s",
        0 != port ? "explicit" : "ssh-config/default",
        '\0' != path[0] ? "nonempty" : "empty");

    /* get local user name */
    if (0 == locuser)
    {
        if (3 >= argc)
        {
            p = userhost;
            while (*p && '@' != *p)
                p++;
            if (*p)
            {
                *p = '\0';
                locuser = userhost;
            }
        }
        else
        {
            locuser = argv[3];
            /* the Cygwin runtime does not remove double quotes around args with non-ASCII chars */
            if (locuser[0] == '"')
            {
                size_t len = strlen(locuser);
                if (len >= 2 && locuser[len - 1] == '"')
                {
                    locuser[len - 1] = '\0';
                    ++locuser;
                }
            }
            for (p = locuser; *p; p++)
                if ('\\' == *p)
                {
                    *p = '+';
                    locuser_nodom = p + 1;
                }
        }
    }

    snprintf(idmap, sizeof idmap, "-ouid=-1,gid=-1");
    if (0 != locuser)
    {
        passwd = getpwnam(locuser);
        if (0 == passwd && 0 != locuser_nodom)
            passwd = getpwnam(locuser_nodom);
        if (0 != passwd)
            snprintf(idmap, sizeof idmap, "-ouid=%d,gid=%d", passwd->pw_uid, passwd->pw_gid);
    }

    diag("svc.identity", "local-user=%s account=%s uid=%ld gid=%ld",
        0 != locuser ? "specified" : "implicit",
        0 != passwd ? "resolved" : "unresolved",
        0 != passwd ? (long)passwd->pw_uid : -1L,
        0 != passwd ? (long)passwd->pw_gid : -1L);

    if (credentials)
    {
        authmeth = "-opassword_stdin,password_stdout";
        if (maxconns > 1)
        {
            diag("svc.config",
                "name=sshfs.max_conns value=%u status=incompatible-with-password-bridge",
                maxconns);
            fprintf(stderr,
                "sshfs-win: sshfs.max_conns must be 1 for password launcher classes\n");
            free(remote);
            free(volpfx);
            return 1;
        }
    }
    else
        authmeth = "-oPreferredAuthentications=publickey,BatchMode=yes,forkless_spawn";

    diag("svc.auth", "mode=%s spawn=%s interactive=%s max_conns=%u",
        credentials ? "password" : "publickey",
        credentials ? "legacy-fork-password-bridge" : "forkless-win32",
        credentials ? "password-bridge" : "disabled",
        maxconns);

    char *sshfs_argv[256] =
    {
        sshfs, SSHFS_ARGS, idmap, authmeth, volpfx, portopt, remote, argv[2], 0,
    };

    if ('\0' == portopt[0])
    {
        int portopt_idx = 0;
        while (sshfs_argv[portopt_idx] != portopt)
            portopt_idx++;
        while (sshfs_argv[portopt_idx] != 0)
        {
            sshfs_argv[portopt_idx] = sshfs_argv[portopt_idx + 1];
            portopt_idx++;
        }
    }

    if (reconnect)
    {
        char *reconnect_argv[] = { "-oreconnect", 0 };
        concat_argv(sshfs_argv, reconnect_argv);
    }

    if (delayconnect)
    {
        char *delay_argv[] = { "-odelay_connect", 0 };
        concat_argv(sshfs_argv, delay_argv);
    }

    if (syncwrite)
    {
        char *sync_argv[] = { "-osshfs_sync", 0 };
        concat_argv(sshfs_argv, sync_argv);
    }

    if (stallwarn != 0)
    {
        char *stall_argv[] = { stallopt, 0 };
        snprintf(stallopt, sizeof stallopt, "-ostall_warn=%u", stallwarn);
        concat_argv(sshfs_argv, stall_argv);
    }

    if (handlewarn != 0)
    {
        char *handle_argv[] = { handleopt, 0 };
        snprintf(handleopt, sizeof handleopt, "-ohandle_warn=%u", handlewarn);
        concat_argv(sshfs_argv, handle_argv);
    }

    if (handlelimit != 0)
    {
        char *limit_argv[] = { limitopt, 0 };
        snprintf(limitopt, sizeof limitopt, "-ohandle_limit=%u", handlelimit);
        concat_argv(sshfs_argv, limit_argv);
    }

    if (maxconns > 1)
    {
        char *maxconn_argv[] = { maxconnopt, 0 };
        snprintf(maxconnopt, sizeof maxconnopt, "-omax_conns=%u", maxconns);
        concat_argv(sshfs_argv, maxconn_argv);
    }

    if (4 < argc)
        concat_argv(sshfs_argv, argv + 4);

    diag("svc.ready",
        "extra-options=%s reconnect=%s delay_connect=%s sync_write=%s stall_warn=%u handle_warn=%u handle_limit=%u max_conns=%u",
        4 < argc ? "yes" : "no", reconnect ? "yes" : "no",
        delayconnect ? "yes" : "no", syncwrite ? "yes" : "no",
        stallwarn, handlewarn, handlelimit, maxconns);
    {
        int exec_errno;
        fixenv_and_execv(sshfs, sshfs_argv);
        exec_errno = errno;

        /* Reached only when execve failed. */
        free(remote);
        free(volpfx);
        errno = exec_errno;
        return ENOENT == exec_errno ? 127 : 126;
    }

#undef SSHFS_ARGS
}

int main(int argc, char *argv[])
{
    const char *mode;

    normalize_launcher_argv(&argc, &argv);

    if (2 > argc)
    {
        diag("argument.error", "reason=missing-mode argc=%d", argc);
        usage();
    }

    mode = argv[1];
    diag("entry", "argc=%d mode=%s", argc, mode);

    if (0 == strcmp("cmd", mode))
        return do_cmd(argc - 1, argv + 1);
    if (0 == strcmp("svc", mode))
        return do_svc(argc - 1, argv + 1);

    diag("argument.error", "reason=unknown-mode mode=%s", mode);
    usage();
    return 1;
}