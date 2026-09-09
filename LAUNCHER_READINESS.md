# Launcher readiness — checkpoint 15

WinFsp Network Provider starts a Launcher-managed file system and then probes the
new volume root with `CreateFileW`. Failures in this path are collapsed heavily
toward `WN_NO_NETWORK` (System error 67), so an SSHFS process that has not yet
entered its FUSE request loop is difficult to distinguish from a dead launch.

SSHFS upstream already provides `-o delay_connect`: mount the FUSE endpoint
first and defer the SSH/SFTP connection until the mount is first accessed.
Checkpoint 15 enables this mode by default only for public-key Launcher
registrations (`sshfs.k` and `sshfs.kr`). Password registrations remain eager.

The resulting startup order is:

1. validate arguments and initialize SSHFS;
2. create the WinFsp/FUSE mount;
3. enter the FUSE request loop without requiring an SSH handshake first;
4. WinFsp Network Provider probes the root;
5. that first FUSE request performs the SSH/SFTP connection.

This does not hide authentication or path errors: the normal Network Provider
root probe still touches the file system and therefore drives the first remote
connection. It does move connection latency out of process-startup readiness.

`sshfs.delay_connect` is a DWORD launcher-class setting. For public-key
classes, `1` enables the mode and `0` restores eager connection. A missing value uses the class default (`1` for public-key classes); a present but invalid value is rejected.

Password classes (`sshfs` and `sshfs.r`) require `sshfs.delay_connect=0`.
A configured value of `1` is rejected instead of being silently rewritten. The
Launcher writes the password secret and waits for `password_stdout` to report
`OK/KO`; that acknowledgment is emitted from `sftp_init`. If SFTP initialization were delayed until the first FUSE request,
the Launcher would wait for `OK` while the Network Provider waited for Launcher
start to return before issuing that request: a startup cycle ending in timeout.

This checkpoint does not change WinFsp itself and does not claim to solve Error
67 cases where the Network Provider never reaches the Launcher process.
