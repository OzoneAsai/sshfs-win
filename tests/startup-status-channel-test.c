#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct startup_status {
    int stage;
    int errnum;
};

static int read_status(int fd, struct startup_status *status)
{
    char *p = (char *)status;
    size_t used = 0;

    while (used < sizeof(*status)) {
        ssize_t n = read(fd, p + used, sizeof(*status) - used);
        if (n > 0) {
            used += (size_t)n;
            continue;
        }
        if (n == 0)
            return used == 0 ? 0 : -2;
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 1;
}

static void write_failure(int fd, int stage, int errnum)
{
    struct startup_status status = { stage, errnum };
    const char *p = (const char *)&status;
    size_t left = sizeof(status);

    while (left != 0) {
        ssize_t n = write(fd, p, left);
        if (n > 0) {
            p += n;
            left -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        _exit(90);
    }
}

static int exec_handoff_case(void)
{
    int p[2], status;
    pid_t pid;
    struct startup_status payload;

    if (pipe(p) != 0)
        return 10;
    if (fcntl(p[1], F_SETFD, FD_CLOEXEC) != 0)
        return 11;

    pid = fork();
    if (pid < 0)
        return 12;
    if (pid == 0) {
        close(p[0]);
        execl("/bin/true", "true", (char *)0);
        write_failure(p[1], 7, errno);
        _exit(91);
    }

    close(p[1]);
    if (read_status(p[0], &payload) != 0)
        return 13;
    close(p[0]);
    if (waitpid(pid, &status, 0) < 0)
        return 14;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 15;
    return 0;
}

static int failure_case(void)
{
    int p[2], status;
    pid_t pid;
    struct startup_status payload;
    int r;

    if (pipe(p) != 0)
        return 20;
    if (fcntl(p[1], F_SETFD, FD_CLOEXEC) != 0)
        return 21;

    pid = fork();
    if (pid < 0)
        return 22;
    if (pid == 0) {
        close(p[0]);
        write_failure(p[1], 7, ENOENT);
        _exit(1);
    }

    close(p[1]);
    r = read_status(p[0], &payload);
    close(p[0]);
    if (waitpid(pid, &status, 0) < 0)
        return 23;
    if (r != 1 || payload.stage != 7 || payload.errnum != ENOENT)
        return 24;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 1)
        return 25;
    return 0;
}

static int partial_payload_case(void)
{
    int p[2];
    struct startup_status payload;
    int half = 1234;

    if (pipe(p) != 0)
        return 30;
    if (write(p[1], &half, sizeof(half)) != (ssize_t)sizeof(half))
        return 31;
    close(p[1]);
    if (read_status(p[0], &payload) != -2)
        return 32;
    close(p[0]);
    return 0;
}

int main(void)
{
    int r;

    r = exec_handoff_case();
    if (r != 0) {
        fprintf(stderr, "exec handoff case failed: %d\n", r);
        return r;
    }
    r = failure_case();
    if (r != 0) {
        fprintf(stderr, "failure payload case failed: %d\n", r);
        return r;
    }
    r = partial_payload_case();
    if (r != 0) {
        fprintf(stderr, "partial payload case failed: %d\n", r);
        return r;
    }

    puts("startup status channel: PASS");
    return 0;
}
