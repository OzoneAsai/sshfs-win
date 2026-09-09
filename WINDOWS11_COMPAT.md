# Windows 11 compatibility status — current checkpoint

The current checkpoint preserves the Unicode normalization work, the fail-closed
Windows integration contract, and a deterministic source-built OpenSSH target.
It still does **not** claim full Windows 11 / 25H2 certification without native execution.

## Error 67 boundaries

1. No wrapper `stage=entry`: pre-wrapper WinFsp Network Provider / Launcher /
   registration / process-creation domain.
2. `stage=argv.native status=active`: original Launcher UTF-16 command line was
   recovered and normalized to strict UTF-8 before UNC parsing.
3. `stage=argv.native ... fallback=cygwin`: reconstruction failed or argc did
   not match; diagnose the Cygwin startup boundary separately.
4. `stage=exec.attempt`: wrapper configuration and argument parsing completed;
   continue into SSHFS/OpenSSH/FUSE/transport.

## Unicode

WinFsp current source remains UTF-16 through Network Provider and Launcher, and
uses `CreateProcessAsUserW`. Cygwin, however, exports a replacement
`GetCommandLineW` reconstructed from its already-created argv. The wrapper
bypasses that replacement by resolving the kernel32 export dynamically in
`svc` mode, then converts the original UTF-16 process command line to strict
UTF-8 and parses Windows quoting locally.

This is intended to cover the wrapper side of historical cases such as Chinese
remote paths and Cyrillic remote usernames. Native Windows tests remain
required before those issues are declared fixed.

## Launcher readiness

Public-key classes (`sshfs.k`, `sshfs.kr`) default to delayed connection and pass `BatchMode=yes` to OpenSSH so Explorer cannot be blocked by an invisible authentication prompt.
Password classes (`sshfs`, `sshfs.r`) remain eager because StartWithSecret waits
for `password_stdout` from SFTP initialization; delaying that initialization
would create a startup dependency cycle.

## Mandatory ASLR

Public-key mounts use checkpoint 12's forkless Win32 SSH spawn path. Password
mounts still use the legacy fork/PTTY bridge and remain outside the Mandatory
ASLR compatibility claim.


## Vendor OpenSSH build

OpenSSH portable defines the client as `ssh$(EXEEXT)`. On Cygwin this is
`ssh.exe`; asking GNU make for `ssh` can fall through to its built-in implicit
rule and attempt to link `ssh.o` alone. All normal, recovery, and finalization
paths now share one locked vendor-SSH definition. The OpenSSH build runs make
with built-in implicit rules disabled, so a future target mismatch fails
immediately rather than producing a misleading unresolved-symbol link.

Version probes run with the vendored OpenSSL `bin` directory first in `PATH`,
so the recorded OpenSSH version is tested against the intended crypto runtime.

## Connection routing concurrency

Multi-connection load and handle-pressure selection reads connection counters
under `sshfs.lock`. The selection primitive is split into a lock-owning wrapper
and a `get_conn_locked` variant for callers such as open/create that already hold
the global lock, avoiding both a C data race and recursive-lock deadlock.

## Write completion integrity

Both synchronous and asynchronous WRITE completion require a well-formed
`SSH_FXP_STATUS` reply. Missing, wrong-type, truncated, or non-OK replies become
errors instead of diagnostic-only messages. The asynchronous `write_error`
state is also read under the same lock used by completion and flush.

## Checkpoint self-containment

`tools/apply-checkpoint.sh` carries `portable-format.h` and treats the optional
top-level patch directory as optional rather than allowing an unmatched glob to
reach `git apply`. The Makefile wrapper target depends on `portable-format.h`,
so header-only edits invalidate incremental builds.

The bundled pristine SSHFS source is now a normal source-archive directory and
does not require nested `.git` metadata. `tools/verify-sshfs-source.sh`
reconstructs its Git tree identity and requires tree
`35655f60d37403663238a73b4204cfd64fdca73c` before the patch series can be
applied. Build staging copies this verified source tree directly; `make clean`
removes only generated `.build` state and also works from an extracted source
archive.

## Remaining release gates

- Windows 11 24H2 and 25H2 Explorer/`net use` matrix.
- Password Unicode and PTY/fork runtime validation.
- Native Cygwin rebuild of the complete current checkpoint, including the shared vendor-SSH build path.
- Handle-pressure stress and Windows HandleCount measurements.
- No automatic watchdog/process recovery is enabled.
