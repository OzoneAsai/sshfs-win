# SSHFS-Win startup diagnostics

The wrapper emits structured startup stages to standard error. This is intended to separate Windows/WinFsp Launcher failures from wrapper, Cygwin, OpenSSH, authentication, and SSHFS failures.

No persistent log is enabled by default.

## Enable logging

Run an elevated PowerShell prompt:

```powershell
.\tools\enable-diagnostics.ps1
```

The WinFsp Launcher will append stderr from future SSHFS-Win mounts to:

```text
%USERPROFILE%\sshfs-win-diagnostics.log
```

Disable it with:

```powershell
.\tools\disable-diagnostics.ps1
```

View the current user's log with:

```powershell
.\tools\show-diagnostics.ps1
```

Disconnect and reconnect an existing network-drive mapping after changing the setting.

## Wrapper stages

The wrapper intentionally does not log passwords, private-key contents, remote host names, or remote path text.

Typical successful wrapper progression is:

```text
stage=entry
stage=svc.entry
stage=svc.class
stage=svc.remote
stage=svc.identity
stage=svc.auth
stage=svc.ready
stage=exec.attempt
```

After `exec.attempt`, a successful `execve` replaces the wrapper process with SSHFS, so there is deliberately no wrapper-side `exec.success` line. Any later stderr text comes from SSHFS/OpenSSH/Cygwin.

### Interpretation

- **No diagnostics file at all**: the WinFsp Launcher did not reach the SSHFS-Win process, or it could not create the configured stderr file. Check the Windows Application event log for WinFsp Launcher/Network Provider failures.
- **`stage=entry` but no `stage=svc.entry`**: command mode/argument dispatch failed before the service parser.
- **`stage=argv.native status=unavailable`**: native UTF-16 argv recovery failed; the line names the failed stage and Win32 error before Cygwin argv fallback.
- **Stops around `stage=svc.class` / `stage=svc.remote`**: investigate UNC parsing, Launcher arguments, and invalid/unreadable class configuration.
- **`stage=svc.identity ... account=unresolved`**: Cygwin could not resolve the local Windows account; this is a strong signal for user-profile/Cygwin identity problems.
- **`stage=svc.auth` then `stage=exec.attempt`**: the wrapper reached the SSHFS handoff. Subsequent failures belong to the SSHFS/OpenSSH/Cygwin/FUSE side rather than UNC parsing.
- **`stage=exec.failure`**: the wrapper could not execute the bundled SSHFS binary. The line includes `errno` and its text.
- **OpenSSH host-key or key-loading errors after `stage=exec.attempt`**: inspect `%USERPROFILE%\.ssh\config`, `known_hosts`, key permissions, and agent availability.

## Windows 11 Error 67

For an Error 67 report, the highest-value first distinction is whether this log reaches `stage=entry` at all.

If it does not, the failure is before the wrapper: Network Provider, Launcher lookup/security, process creation, or stderr-path creation.

If it reaches `stage=exec.attempt`, the Network Provider and Launcher have already done enough to start SSHFS-Win, and investigation can move to Cygwin/OpenSSH/SSHFS/FUSE.
