# CP29 CodeGraph processing report

## Scope

This checkpoint was derived from the CP28 source archive while using the supplied
CP27 Windows/Cygwin build transcript as observed failure evidence. CodeGraph 1.5.0
was initialized against the source tree and synchronized after changes.

CodeGraph final indexed state before packaging:
- 42 indexed files
- 727 nodes
- 1,874 edges
- backend: node:sqlite

CodeGraph parses the C/Python portions of this tree. Shell, PowerShell, registry,
and Markdown files were inspected directly and are guarded by static contract
tests where appropriate.

## Structural findings and changes

### OpenSSH vendor build target

The transcript showed GNU make taking the built-in `ssh <- ssh.o` implicit rule,
which linked only `ssh.o` and produced a large undefined-reference cascade. The
pinned OpenSSH Makefile defines the real Cygwin target as `ssh$(EXEEXT)`, hence
`ssh.exe`.

CP29 centralizes the vendor SSH build model in `tools/vendor-ssh-common.sh`.
Both the normal and continuation paths use `OPENSSH_BUILD_TARGET=ssh.exe` and
invoke `make -r`, disabling GNU make built-in rules so a future target mismatch
fails immediately instead of producing a bogus standalone link.

OpenSSL/OpenSSH versions, refs, and commits are read from
`deps/SOURCE_LOCKS.tsv`; duplicate hard-coded lock values were removed from the
three vendor build entry points. Runtime probes execute with the vendored OpenSSL
directory first in PATH and verify the observed OpenSSH/OpenSSL versions against
the lock file.

### Cygwin wrapper allocation portability

The transcript showed Cygwin rejecting `asprintf` as an implicit declaration in
`sshfs-win.c`. CP29 replaces wrapper `asprintf` usage with
`sshfs_win_alloc_printf`, a small two-pass `vsnprintf` allocator in
`portable-format.h`.

CodeGraph impact for `sshfs_win_alloc_printf` reaches the intended wrapper
configuration/service path (`reg_get_dword`, `reg_bool`, `reg_uint`, `do_svc`,
`main`) and the new formatter unit test, without introducing unrelated source
dependencies.

### Windows diagnostic native-process semantics

The transcript showed valid OpenSSH stderr output being converted by Windows
PowerShell into diagnostic failures, and an empty `-ArgumentList @()` breaking
the wrapper smoke test.

`tools/diagnose-windows11.ps1` now has one captured-process path that separates
stdout, stderr, and exit status. Empty argument lists are omitted from
`Start-Process`. OpenSSH `-G` parses stdout while stderr is retained as
diagnostic information rather than treated as process failure.

### WinFsp Launcher registry view

The transcript's successful investigation established that WinFsp Launcher uses
the 32-bit registry view on x64 Windows. CP29 therefore preserves the explicit
`WOW6432Node` physical path in legacy `.reg` imports, where no RegistryView API
exists, while PowerShell code uses `RegistryView.Registry32`/`Registry64`
explicitly.

The two legacy `.reg` files no longer contain a BOM.

### Build/tooling cleanup

- Meson setup uses the explicit `meson setup ..` form.
- `CHECKPOINT` is the single artifact identity and is set to `29`.
- Current-status Windows documents no longer bake a stale checkpoint number into
  their title.
- `tools/apply-checkpoint.sh` carries `CHECKPOINT`.
- Static tooling tests now guard the vendor SSH model, BOM policy, registry-view
  model, portable allocator, diagnostic process capture, Meson invocation, and
  checkpoint identity.

## Validation performed in this environment

- CodeGraph index synchronized successfully.
- `sshfs-win.c`: strict Linux compile with `-std=gnu23 -Wall -Wextra -Werror`:
  PASS.
- C tests: 20/20 PASS with `-std=gnu23 -Wall -Wextra -Werror -pthread`.
- `tests/windows-tooling-static-test.py`: PASS.
- `tests/dist-signing-contract-test.sh`: PASS.
- `bash -n` over all shell tests/tools: PASS.
- BOM scan over checkpoint source: PASS (none found).

A full native Cygwin/Windows 11 rebuild and mount test was not executed in this
Linux sandbox. That remains the release gate for the resulting checkpoint.
