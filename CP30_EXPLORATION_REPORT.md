# CP30 CodeGraph exploration and correctness report

## Scope

CP30 continues from the CP29 processed source. The supplied CodeGraph 1.5.0
binary was used on both the checkpoint source tree and a clean, fully patched
SSHFS tree. The uploaded Windows/Cygwin transcript remains observational
evidence; new changes were accepted only where the source established a
reproducible defect.

Final checkpoint source graph after the changes:
- 43 indexed files
- 747 nodes
- 1,906 edges
- backend: node:sqlite

Clean final SSHFS patch-stack graph:
- 16 indexed files
- 439 nodes
- 1,268 edges

## New defects found and fixed

### 1. Wrapper service-option lifetime

`do_svc()` placed `stallopt`, `handleopt`, `limitopt`, and `maxconnopt` arrays
inside individual `if` blocks and retained their addresses in `sshfs_argv`.
Those arrays cease to exist when the blocks end, before the final `execve`.
This is C use-after-scope undefined behaviour when any corresponding launcher
setting is enabled.

The option storage now has `do_svc()` function lifetime while the small temporary
argv arrays remain local because only their string pointers are copied.

### 2. Write protocol errors were diagnostic-only

The WRITE callbacks only set an error when a transport error existed or when a
well-formed STATUS carried a non-OK code. An unexpected reply packet or malformed
STATUS printed `protocol error` but could still complete as success.

A shared write-reply validator now requires:
- a reply,
- `SSH_FXP_STATUS`,
- a complete status integer,
- `SSH_FX_OK`.

Transport errors and non-OK SFTP statuses are propagated. Both synchronous and
asynchronous callbacks use the same validator.

### 3. Async `write_error` data race

WRITE completion and flush mutate/read `sf->write_error` under `sshfs.lock`, but
the asynchronous-write selection path previously read it without that lock.
The read now uses the same lock before deciding whether to remain asynchronous
or fall back to synchronous writing.

### 4. Multi-connection routing data race

`get_conn()` scored `req_count`, `dir_count`, `file_count`, transport state,
remote-handle counts, and handle reservations without the lock used to mutate
those fields. Calling the selection "best effort" does not make unsynchronized C
reads defined.

The routing primitive is now split:
- `get_conn()` acquires `sshfs.lock`;
- `get_conn_locked()` performs lookup/selection for already-locked callers.

`sshfs_open_common()` already owns `sshfs.lock`, so it uses the locked variant
directly rather than recursively acquiring the mutex.

### 5. Checkpoint application was not self-contained

CP29 introduced `portable-format.h`, but `tools/apply-checkpoint.sh` copied
`sshfs-win.c` without copying that header. A checkpoint applied to the pinned
base would therefore contain an include of a file that was never installed.

The script also iterated an unmatched
`top-level-patches/*.patch` glob even though the distributed checkpoint has no
such directory. With `set -e`, that literal path could reach `git apply`.

The header is now copied and the optional patch set is collected with Bash
`nullglob`.

### 6. Incremental wrapper build ignored its header

The Makefile wrapper target depended on `sshfs-win.c` but not
`portable-format.h`, permitting a header-only change to leave a stale
`sshfs-win` binary. The header is now an explicit prerequisite.

### 7. Native Windows argv parser was self-consistent but not CRT-complete

The property suite quoted arguments with the project's own generator and parsed
them with the project's own parser. That proves round-trip consistency, but both
sides could share the same grammar error.

Microsoft's documented C-runtime grammar additionally states that a pair of
double quotes inside a quoted span is one literal quote. The old parser dropped
that literal quote. The parser and fixture now implement this rule and the test
suite contains the documented `a"b"" c d` case in addition to 2,000 randomized
round trips.

## Validation

- All 24 SSHFS patches applied cleanly, in order, to a fresh copy of the pinned
  bundled SSHFS source.
- Strict C unit tests: 21/21 PASS with
  `-std=gnu23 -Wall -Wextra -Werror -pthread`.
- The same 21 tests: PASS under AddressSanitizer + UndefinedBehaviorSanitizer.
- `sshfs-win.c`: strict compile PASS with
  `-std=gnu23 -Wall -Wextra -Werror`.
- `tests/windows-tooling-static-test.py`: PASS.
- `tests/dist-signing-contract-test.sh`: PASS.
- `bash -n` over checkpoint shell tools/tests: PASS.
- Microsoft command-line corpus + 2,000 randomized argv round trips: PASS.

## Remaining release gates

This Linux environment does not replace a native Cygwin/Windows 11 build.
Required final gates remain:
- complete Cygwin x64 rebuild from the checkpoint,
- MSI install/upgrade,
- WinFsp Launcher/Network Provider mount matrix on Windows 11 24H2 and 25H2,
- synchronous and asynchronous write fault injection against a real SFTP
  transport,
- multi-connection/handle-pressure stress on the native build,
- Unicode UNC/path/username matrix through Explorer and `net use`.

No public GitHub issue was treated as proof of a root cause. Issue reports were
used only to prioritize source paths for inspection.
