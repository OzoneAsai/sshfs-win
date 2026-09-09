# Windows write-safety policy

SSHFS upstream permits asynchronous writes by default. Without `-o sshfs_sync`,
`write()` may return to userspace before the SFTP server has replied. A later
transport failure can therefore surface at flush/release after the application
has already observed a successful write.

For the Windows Launcher classes this checkpoint defaults
`sshfs.sync_write=1`, which adds `-osshfs_sync`. The SFTP response is therefore
observed before SSHFS returns the write result to WinFsp. This is deliberately a
reliability-over-throughput default for Windows 11.

The upstream default for other platforms is unchanged. An administrator can
restore asynchronous writes with `tools/disable-safe-writes.ps1` or re-enable
the safety policy with `tools/enable-safe-writes.ps1`.

This policy also complements the lazy write-condition lifecycle: when
`sshfs_sync` is active, per-file asynchronous-write condition variables are not
allocated.

## SFTP write reply contract

Write completion now fails closed. A request is successful only when a reply was
received, its packet type is `SSH_FXP_STATUS`, the STATUS payload contains a
complete status code, and that code is `SSH_FX_OK`. Missing replies, unexpected
packet types, truncated STATUS payloads, transport errors, and non-OK SFTP
statuses are propagated as write errors.

Safe synchronous writes also use a per-chunk remote commit barrier. SSHFS waits
for an `SSH_FX_OK` STATUS for each SFTP WRITE before submitting the next chunk.
If a later chunk fails, the write returns the already committed prefix as a
short write; if the first chunk fails, the original error is returned. This
prevents an `O_APPEND` retry from duplicating a prefix that the server had
already appended before a later chunk failed.

Asynchronous mode remains supported. Its per-file `write_error` state is read
under `sshfs.lock`, matching the callback/flush mutation lock, so the optional
asynchronous path no longer relies on an unsynchronized C data race.
