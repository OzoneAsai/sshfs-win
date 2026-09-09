<h1 align="center">
    <img src="art/sshfs-glow.png" width="256"/>
    <br/>
    <br/>
    SSHFS-Win &middot; Windows modernization fork
</h1>

This repository is a development fork of [upstream SSHFS-Win](https://github.com/winfsp/sshfs-win). It retains the WinFsp/Cygwin architecture while carrying a substantially larger Windows 11, concurrency, transport-generation, write-integrity, reproducibility, and diagnostic patch set.

> **Release status:** this fork does not currently advertise an OzoneAsai binary release as a stable download. The upstream WinGet package and upstream release page install the upstream project, not the source in this repository. Use them when you want upstream SSHFS-Win; do not use them as a way to install this fork.

The package version used by this source tree is stored in [`VERSION`](VERSION). Build timestamps are metadata only and are not part of the MSI product version.

## Current modernization scope

The current source includes, among other work:

- explicit request, handle, and connection-generation ownership in the patched SSHFS core;
- synchronous Windows write safety with validated SFTP STATUS replies;
- multi-connection routing and pathname-affinity fixes;
- forkless Win32 OpenSSH spawning for public-key launcher classes;
- UTF-16 Launcher command-line recovery and strict UTF-8 normalization;
- pinned source-built runtime dependencies and source-tree identity verification;
- source, packaging, sanitizer, and Windows-tooling contract tests.

The remaining native release gates are documented in [`WINDOWS11_COMPAT.md`](WINDOWS11_COMPAT.md).

## Installation

A packaged stable installer for this fork is not currently published from this repository.

For upstream SSHFS-Win, install the latest [WinFsp](https://github.com/winfsp/winfsp/releases/latest) and use the upstream [SSHFS-Win releases](https://github.com/winfsp/sshfs-win/releases). The WinGet command below also installs upstream SSHFS-Win, not this fork:

```console
winget install SSHFS-Win.SSHFS-Win
```

For this fork, build from the checked-out source as described in [Building](#building).

## Basic Usage

Once WinFsp and an SSHFS-Win build are installed, you can map a network drive to a directory on an SSHFS host using Windows Explorer or the `net use` command.

### Windows Explorer

In Windows Explorer select This PC > Map Network Drive and enter the desired drive letter and SSHFS path using the following UNC syntax:

    \\sshfs\REMUSER@HOST[\PATH]

The first time you map a password-authenticated SSHFS path you will be prompted for the SSHFS username and password. You may choose to save these credentials with Windows Credential Manager.

To unmap the drive, right-click the drive icon and select Disconnect.

### Command Line

You can map a network drive from the command line with `net use`:

```console
net use X: \\sshfs\REMUSER@HOST
```

List mappings with:

```console
net use
```

Remove a mapping with:

```console
net use X: /delete
```

## UNC Syntax

The complete UNC syntax is:

    \\sshfs\[LOCUSER=]REMUSER@HOST[!PORT][\PATH]
    \\sshfs.r\[LOCUSER=]REMUSER@HOST[!PORT][\PATH]
    \\sshfs.k\[LOCUSER=]REMUSER@HOST[!PORT][\PATH]
    \\sshfs.kr\[LOCUSER=]REMUSER@HOST[!PORT][\PATH]

- `REMUSER` is the remote user.
- `HOST` is the SSH/SFTP host or an OpenSSH config alias.
- `PORT` is the optional remote port; when omitted, OpenSSH config/default resolution is preserved.
- `PATH` is interpreted relative to the remote home for `sshfs`/`sshfs.k` and relative to remote root for `sshfs.r`/`sshfs.kr`.
- `LOCUSER` is the optional local Windows account (`USERNAME` or `DOMAIN+USERNAME`).
- `sshfs.k` and `sshfs.kr` are public-key launcher classes. They use non-interactive OpenSSH policy and the forkless Win32 spawn path by default.

## GUI front ends

Existing SSHFS-Win front ends may still be useful because the UNC surface remains compatible:

- [SiriKali](https://mhogomchungu.github.io/sirikali/)
- [SSHFS-Win-Manager](https://github.com/evsar3/sshfs-win-manager)
- [NEO SSH-Win Manager](https://github.com/gregorkrebs/neosshwinmanager)

Compatibility with a particular frontend does not imply that the frontend installs or tests this fork.

## Using Jump Hosts

SSHFS-Win can use normal OpenSSH configuration. Prefer expressing jump-host behavior in `%USERPROFILE%\.ssh\config`, for example with `ProxyJump`, and then mount the configured alias through the normal UNC syntax.

A local port forward is also possible when that better matches the environment:

```console
ssh -L LOCAL_PORT:TARGET_HOST:TARGET_PORT JUMP_HOST
net use X: \\sshfs\REMUSER@localhost!LOCAL_PORT
```

## Advanced Usage

The `sshfs-win.exe` wrapper and `sshfs.exe` core can be invoked directly. In the installed tree they live under `bin`.

`sshfs-win.exe` supports:

```text
usage: sshfs-win cmd SSHFS_COMMAND_LINE
    SSHFS_COMMAND_LINE  command line to pass to sshfs

usage: sshfs-win svc PREFIX X: [LOCUSER] [SSHFS_OPTIONS]
    PREFIX              Windows UNC prefix
                        \sshfs[.SUFFIX]\[LOCUSER=]REMUSER@HOST[!PORT][\PATH]
                        sshfs: remote user home dir
                        sshfs.r: remote root dir
                        sshfs.k: remote user home dir with key authentication
                        sshfs.kr: remote root dir with key authentication
    LOCUSER             local user (DOMAIN+USERNAME)
    REMUSER             remote user
    HOST                remote host
    PORT                remote port
    PATH                remote path (relative to remote home or root)
    X:                  mount drive
    SSHFS_OPTIONS       additional options to pass to SSHFS
```

`sshfs-win.exe` is a Windows/WinFsp wrapper and should not be treated as a generic Cygwin command-line frontend.

## Launcher settings

WinFsp Launcher service definitions live in the **32-bit registry view**. Code in this repository accesses that view through the .NET `RegistryView.Registry32` abstraction rather than by spelling the physical `WOW6432Node` path.

Use the shared helper-backed PowerShell tools from an elevated PowerShell session when changing Launcher configuration. They verify writes and fail on inaccessible or malformed configuration.

### Preventing timeouts

To set `ServerAliveInterval=30` for the standard launcher classes:

```powershell
.\tools\set-server-alive-interval.ps1
```

### Setting looser permissions for new files and directories

To apply `create_file_umask=0117,create_dir_umask=0007`:

```powershell
.\tools\set-group-read-write.ps1
```

The old `.reg` mutation files were removed because a `.reg` file cannot express a registry view independently of the physical redirected path.

## Diagnostics

Startup diagnostics and the Windows 11 Error 67 boundary are documented in [`DIAGNOSTICS.md`](DIAGNOSTICS.md). Persistent logging is opt-in.

Useful configuration and inspection helpers live under `tools/`, including reconnect, delayed connection, safe-write, parallel-connection, handle-observer, handle-limit, and diagnostic toggles.

## Project Organization

This fork is no longer accurately described as a wrapper plus a couple of patches.

- `sshfs/` is a **vendored, pinned pristine SSHFS source directory**, not a Git submodule. Its content and executable file modes are verified against the pinned upstream Git tree before patching.
- `patches/SERIES` is the canonical ordered patch manifest for the effective SSHFS implementation.
- `sshfs-win.c` is the WinFsp/Cygwin wrapper and Windows Launcher boundary.
- `sshfs-win.wxs` describes the current MSI package.
- `VERSION` is the deterministic MSI product-version source.
- `deps/` contains dependency policy and exact source locks.
- `tests/` contains C, shell, Python, sanitizer-oriented, and packaging/tooling contracts.
- `.github/workflows/contracts.yml` runs the portable source contracts on GitHub Actions.
- `Makefile` drives dependency construction, SSHFS patching, runtime closure assembly, and MSI packaging.

Historical checkpoint reports remain in the repository as engineering records; current behavior should be inferred from the source and executable contracts rather than from a checkpoint report alone.

## Building

The build is currently Cygwin-hosted and packages a Windows MSI.

The dependency policy and pinned versions are defined in [`DEPENDENCY_POLICY.md`](DEPENDENCY_POLICY.md) and [`deps/SOURCE_LOCKS.tsv`](deps/SOURCE_LOCKS.tsv). The default build constructs the pinned vendor runtime rather than accepting arbitrary DLLs from the developer machine.

At minimum the build environment needs the bootstrap tooling checked by:

```console
tools/check-dependencies.sh --bootstrap
```

Then build from the repository root:

```console
make
```

The resulting distribution MSI is staged under `.build/<arch>/dist/` after the signing policy succeeds. For deliberate local testing only, an unsigned package can be requested with `AllowUnsigned=1`.

No `git submodule update` step is required: `sshfs/` is shipped as verified plain source.

### Packaging toolchain status

The current Makefile still uses the WiX v3 command-line pipeline (`candle`, `heat`, `light`). Moving packaging to the supported WiX v7 SDK/build model is a remaining standardization task and should be performed as a dedicated packaging change rather than hidden behind compatibility wrappers.

## Validation

Portable validation is designed to be runnable outside Windows where possible:

```console
python3 tests/packaging-standard-contract-test.py
python3 tests/windows-tooling-static-test.py
bash tests/patch-series-contract-test.sh
bash tests/source-archive-self-contained-test.sh
bash tests/artifact-hygiene-test.sh
bash tests/dist-signing-contract-test.sh
```

The root GitHub Actions workflow additionally strict-compiles the wrapper and runs the standalone C contracts under both normal strict flags and AddressSanitizer/UndefinedBehaviorSanitizer.

These portable tests do not replace the native Cygwin/WinFsp/Windows 11 release gates documented in `WINDOWS11_COMPAT.md`.

## License

SSHFS-Win uses the same GPLv2+ licensing basis as SSHFS. It interfaces with WinFsp under WinFsp's applicable licensing terms.

The distribution also carries third-party runtime components whose exact versions are locked in `deps/SOURCE_LOCKS.tsv`; consult those upstream projects for their respective license texts.
