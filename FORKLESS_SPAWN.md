# Forkless SSH launch on Cygwin — checkpoint 12

Cygwin fork requires DLLs in a forked child to be mapped compatibly with the parent. Mandatory ASLR can prevent that and produce the familiar `dofork ... 0xC0000142` / `Resource temporarily unavailable` failure class.

SSHFS upstream's `start_ssh()` performs two `fork()` calls before `execvp()`. Checkpoint 12 adds an opt-in Cygwin-specific path that launches the SSH process directly with `CreateProcessW` instead.

## Scope

The SSHFS option `forkless_spawn` enables the new launcher. The SSHFS-Win wrapper requests it automatically for public-key authentication. The password bridge keeps the legacy PTY/fork path because its current password prompt interception depends on a Cygwin controlling PTY.

`no_forkless_spawn` is provided as an escape hatch for direct troubleshooting and may be appended after wrapper options.

## Handle model

The child receives exactly three inherited handles through `STARTUPINFOEX` and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`: stdin, stdout, stderr. SFTP stdin/stdout use two unidirectional Win32 anonymous pipes. The parent ends are converted to Cygwin file descriptors with `cygwin_attach_handle_to_fd`, so the existing SSHFS read/write code continues to use POSIX fds.

The process-global `OPENSSH_STDIO_MODE=nonsock` setting is installed once during SSHFS processing initialization, before connection threads start, instead of mutating the environment during reconnect.

## Remaining release gate

The Linux environment can validate command-line quoting, patch application, wrapper policy, and static ownership structure, but cannot execute Cygwin's HANDLE-to-fd bridge or WinFsp. Windows 11 runtime validation remains mandatory before this path is declared production-ready.
