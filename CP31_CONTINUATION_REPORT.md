# CP31 continuation report

## Exploration identity

CodeGraph is run only against a fresh effective SSHFS tree produced by applying
the ordered patch stack to the pinned pristine `sshfs/` source. The top-level
`sshfs/` directory itself remains pristine and is not treated as the effective
implementation.

## CP31-D — request ID wraparound

SFTP request IDs are 32-bit and eventually wrap. The request table previously
inserted a new request without checking whether the wrapped ID still belonged to
an in-flight request. CP31-D allocates request-table IDs while holding
`sshfs.lock` and skips occupied IDs rather than replacing their owners.

Validation:
- 28 patches cleanly applied to pristine SSHFS.
- request-ID wraparound regression test: PASS.
- CodeGraph effective tree: 16 files / 441 nodes / 1,275 edges.

## CP31-E — directory reopen handle-limit ordering

`sshfs_reopen_dir()` used to send CLOSE without waiting for its STATUS reply and
then immediately issue OPENDIR. Remote-handle accounting retires a handle only
when the successful CLOSE reply is processed, so `handle_limit=1` saw the old
handle as still live and rejected the replacement OPENDIR with `-EMFILE`.

A reopen is now modeled as a replacement transition: wait for CLOSE completion,
then admit OPENDIR. Ordinary release remains asynchronous.

Validation:
- 29 patches cleanly applied to pristine SSHFS.
- 26/26 strict C tests PASS.
- 26/26 AddressSanitizer + UndefinedBehaviorSanitizer C tests PASS.
- dedicated directory-reopen handle-limit ordering test: PASS.
- CodeGraph effective tree: 16 files / 441 nodes / 1,275 edges.

## CP31-F — native argv backslash/whitespace boundary and checkpoint identity

The native Windows command-line parser checked unquoted whitespace before a
backslash run but not after it. In the CRT grammar backslash only has special
meaning before a quote; it does not escape an unquoted space or tab. Therefore
an unquoted argument ending in `\` could absorb the separator and the next
argument. This can affect UNC service prefixes that end in a backslash.

The parser now re-checks the delimiter after copying a non-quote backslash run.
The production parser and its standalone fixture carry the same rule, and the
corpus now includes unquoted trailing-backslash and UNC-prefix cases.

While running the complete tooling contract, the test suite itself was found to
hard-code checkpoint 30 even though `CHECKPOINT` is 31. That duplicate identity
was replaced with a structural contract: `CHECKPOINT` must be a positive integer,
`apply-checkpoint.sh` must propagate it, and current-status document titles must
not embed any checkpoint number.

Validation:
- Windows native argv corpus + 2,000 randomized quoted round-trips: PASS.
- 26/26 strict C tests: PASS.
- 26/26 AddressSanitizer + UndefinedBehaviorSanitizer tests: PASS.
- `sshfs-win.c` strict compile: PASS.
- Windows tooling static contract: PASS.
- distribution signing contract: PASS.

## CP31-G — in-flight READ request owns chunk lifetime

A read chunk can be cached in `sf->readahead`, but its SFTP READ requests may
still be in flight when the cache owner is dropped by `release()` or by cache
replacement.  The completion callback stores state through `read_req->sio`,
which is embedded in the chunk.  Therefore the cache/read-call reference alone
is not sufficient to keep callback state alive.

Each READ request now takes one chunk reference in `sshfs_read_begin()` and
returns it at the end of `sshfs_read_end()`.  This models request lifetime as an
independent owner: dropping the cache/read-call reference cannot free the chunk
until every submitted READ callback has finished.

Validation:
- 30 patches cleanly applied to pristine SSHFS.
- dedicated read-request chunk lifetime test: strict, ASan+UBSan, and TSan PASS.
- 27/27 strict C tests PASS.
- 27/27 AddressSanitizer + UndefinedBehaviorSanitizer C tests PASS.
- Windows tooling static contract PASS.
- distribution signing contract PASS.
- all shell scripts pass `bash -n`.
- BOM scan: zero files.
- `sshfs-win.c` strict `-std=gnu23 -Wall -Wextra -Werror` compile PASS.
- CodeGraph effective tree: 16 files / 441 nodes / 1,276 edges.

## CP31-H — request completion handoff and async readdir send-failure ownership

`send_iov()` can fail after a request has been published in `reqtab`. If the
sender can no longer remove that entry, the reply or teardown path may already
own the request and still be writing completion state. The previous waiter
looked at `req->error` first and could free immediately, bypassing the ready
semaphore and racing that completion owner.

Requests now record `completion_expected` when published. A waiter consumes the
ready handoff before freeing whenever completion ownership has left the sender,
even if a local send error is already visible. If send failure successfully
reclaims the request from `reqtab`, completion ownership is cancelled and no
wait is required.

This also closes an async READDIR leak: `sftp_readdir_send()` may return an
error together with a request object. `sftp_readdir_async()` now consumes and
frees that errored request instead of abandoning its semaphore and
`conn->req_count` ownership.

Validation:
- 31 patches cleanly applied to pristine SSHFS.
- dedicated request-completion handoff test: strict, ASan+UBSan, and TSan PASS.
- 28/28 strict C tests PASS.
- 28/28 AddressSanitizer + UndefinedBehaviorSanitizer C tests PASS.
- Windows tooling static contract PASS.
- distribution signing contract PASS.
- all shell scripts pass `bash -n`.
- BOM scan: zero files.
- `sshfs-win.c` strict `-std=gnu23 -Wall -Wextra -Werror` compile PASS.
- CodeGraph effective tree: 16 files / 441 nodes / 1,276 edges.

## CP31-I — per-connection SFTP capability domains

SFTP protocol version and extension advertisements were process-global even
though reconnect and `max_conns` create independent transport sessions. A
capability learned on one connection could therefore authorize an EXTENDED
request on a different generation or connection that had not advertised it.

Protocol version plus POSIX rename, STATVFS, hardlink and fsync capabilities now
belong to `struct conn` and are cleared before each SFTP INIT handshake.
Capability-consuming operations snapshot the selected connection together with
its generation and send through `sftp_request_gen()`. Handle-bound fsync reads
the capability from the handle's own connection after validating its generation.

During review, `statfs()` was also corrected to establish the selected lazy
connection before deciding whether STATVFS is available. Reading the zeroed
capability fields of an unconnected connection would otherwise silently return
the synthetic fallback capacity even when the server supports STATVFS.

Validation:
- 32 patches cleanly applied to pristine SSHFS and reproduced the audited effective source exactly.
- 30/30 strict C tests PASS.
- 30/30 AddressSanitizer + UndefinedBehaviorSanitizer C tests PASS.
- dedicated capability generation and lazy-STATVFS tests PASS.
- Windows tooling static contract PASS.
- distribution signing contract PASS.
- all shell scripts pass `bash -n`.
- BOM scan: zero files.
- `sshfs-win.c` strict `-std=gnu23 -Wall -Wextra -Werror` compile PASS.
- CodeGraph effective tree: 16 files / 443 nodes / 1,280 edges.

## CP31-J — stale-generation flush drains async WRITE ownership

`sshfs_flush()` previously returned `-EIO` immediately when a file handle's
connection generation was stale. That shortcut was unsafe for async writes: a
WRITE request is linked into `sf->write_reqs` before publication and can still
be owned by the sender/cleanup path when teardown changes `connver`. Returning
from flush immediately allowed `release()` to destroy `sf` and its condition
variable while the completion callback still dereferenced them.

Async flush now drains the pre-flush write set under `sshfs.lock` even when the
connection generation has changed. After callback ownership is gone it reports
a concrete accumulated write error first, otherwise the generic stale-session
`-EIO`. Synchronous-write mode keeps the generation check but has no background
`sf->write_reqs` ownership to drain.

Validation:
- 33 patches cleanly applied to pristine SSHFS and reproduced the audited effective source exactly.
- 31/31 strict C tests PASS.
- dedicated stale-flush write-drain test: strict, ASan+UBSan and TSan PASS.
- CodeGraph effective tree: 16 files / 443 nodes / 1,280 edges.

## CP31-K — HANDLE structural validation and explicit patch series

A reply was previously counted as a live remote handle solely because its SFTP
packet type was `SSH_FXP_HANDLE`. `sftp_request_wait()` likewise accepted that
type without validating the encoded handle string. A truncated length field,
length mismatch or trailing bytes could therefore create a phantom entry in
remote-handle accounting and make OPEN/OPENDIR succeed with an unusable opaque
handle. HANDLE replies are now structurally validated before both accounting
and waiter success.

Adding this patch also exposed a build-order defect: patch application depended
on the lexical order of `patches/*.patch`. The first three-digit patch name
(`100-...`) sorts between `10-...` and `20-...`, so the patch stack ceased to be
reproducible as soon as the two-digit namespace was exhausted. `patches/SERIES`
is now the canonical order. The Makefile patch target, source verifier and
checkpoint copier consume/preserve that manifest, and the patch target depends
on the manifest plus every listed patch so edits invalidate the build stamp.

Validation:
- `patches/SERIES` contract PASS: 34 patches, complete and unique.
- 34/34 patches cleanly apply in manifest order and reproduce the audited effective `sshfs.c` exactly.
- 32/32 strict C tests PASS.
- HANDLE reply contract and remote-handle accounting tests PASS under ASan+UBSan.
- Windows tooling static contract PASS.
- distribution signing contract PASS.
- all shell scripts pass `bash -n`.
- BOM scan: zero files.
- CodeGraph verified effective tree: 16 files / 444 nodes / 1,284 edges.

## CP31-L — patch dependency discovery without parse-time side effects

The first SERIES integration derived Make prerequisites by running `sed` over
`patches/SERIES` during Makefile parsing. That made unrelated isolated targets
(such as the distribution-signing contract fixture) touch a patch manifest they
do not need. Dependency invalidation now uses Make's `wildcard` over patch files,
while patch *application order* remains exclusively controlled by SERIES.

Validation:
- patch-series contract PASS (34 patches).
- distribution-signing contract PASS with no SERIES lookup warning.
- Windows tooling static contract PASS.

## CP31-M — UNC remote/path boundary normalization

The launcher translates Windows separators to `/` before parsing an instance.
At the remote/path boundary, repeated separators such as
`\\sshfs\user@host\\etc` therefore became `user@host//etc`. The parser consumed
only the first separator and retained `/etc` as the remote path. In a home-class
launcher this accidentally promoted a path that should remain home-relative to
an absolute SFTP path, bypassing the explicit `.r` / `.kr` root-class contract.

`parse_instance_spec()` now consumes all repeated separators at the one
remote/path boundary and exposes only the relative path payload. Root semantics
remain an explicit launcher-class decision rather than an accident of separator
count.

Validation:
- wrapper parser covers `//path`, `////path`, and separator-only tails.
- CodeGraph impact is limited to `parse_instance_spec`, `do_svc`, wrapper `main`, and the parser-test helpers.
- 32/32 strict C tests PASS.
- standalone wrapper strict compile (`gnu23`, `-Wall -Wextra -Werror`) PASS.
- Windows tooling static contract PASS.
- patch-series contract PASS (34 patches).
- distribution-signing contract PASS.
- all shell scripts pass `bash -n`.
- BOM scan: zero files.

## CP31-N — same-path rename preserves connection affinity

When multi-connection routing is enabled, a successful rename updates the
path-to-connection table. For `rename(path, path)`, the old implementation
replaced the same hash key and immediately removed it. The remote operation is a
no-op, but the local affinity entry disappeared, so later opens of the same path
could be routed to a different connection while already-open objects retained
the original connection.

The conntab update is now skipped when `from` and `to` are identical. Normal
rename-over-destination behavior is unchanged: the source entry becomes the new
path owner while any displaced destination entry may live only through existing
open-object references.

Validation:
- dedicated rename-affinity contract PASS under strict compile and ASan+UBSan.
- 35/35 patches cleanly apply in SERIES order and reproduce the audited effective source byte-for-byte.
- 33/33 strict C tests PASS.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; BOM scan: zero files.
- CodeGraph impact of `sshfs_rename` is local to the function/file.

## CP31-O — OPEN metadata pinned to the handle generation

`sshfs_open_common()` already captured the connection generation used for
`SSH_FXP_OPEN`, but the immediately-following STAT/LSTAT used an unpinned
`sftp_request()`. If reconnect occurred after the OPEN reply but before the
metadata lookup, one logical open could combine a handle from the old SFTP
session with attributes from the new session and return success with a handle
that was already stale.

The metadata request now uses `sftp_request_gen()` with `sf->connver`, making
OPEN plus its validation metadata one session-generation transaction. A
reconnect in the middle makes the open fail with `-EIO` instead of publishing a
stale file object.

Validation:
- dedicated open-generation transaction test PASS under strict and ASan+UBSan.
- 36/36 patches cleanly apply in SERIES order and reproduce the audited effective source byte-for-byte.
- 34/34 strict C tests PASS.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; BOM scan: zero files.
- CodeGraph impact reaches only open/create/truncate callers of `sshfs_open_common()`.

## CP31-P — malformed READDIR replies are protocol I/O errors

`buf_get_entries()` already mapped most truncated `SSH_FXP_NAME` fields to
`-EIO`, but a missing/truncated `longname` left its local parser sentinel at
`-1`.  That raw value could escape through synchronous or asynchronous readdir;
FUSE interprets `-1` as `-EPERM`, so malformed server data appeared to users as
"Permission denied" instead of a transport/protocol failure.

The entry parser now maps a malformed `longname` directly to `-EIO` while
preserving semantic errors returned by attribute/id mapping (for example
`-EPERM`).

Validation:
- 37/37 patches cleanly apply in SERIES order and reproduce the audited effective source byte-for-byte.
- dedicated readdir reply contract PASS under strict and ASan+UBSan.
- 35/35 strict C tests PASS.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; BOM scan: zero files.
- CodeGraph effective tree: 16 files / 444 nodes / 1,284 edges.

## CP31-Q — multi-connection diagnostic statistics synchronization

Each SFTP connection owns a reply-processing thread. With `max_conns > 1`, the
threads updated process-global debug RTT/message counters concurrently without
`sshfs.lock`, while send counters already lived in that lock domain. Although
the fields are diagnostic-only, unsynchronized C accesses are a data race and
therefore undefined behavior.

Reply-side statistics updates now use `sshfs.lock`. Final debug reporting takes
a consistent locked snapshot before formatting output, so shutdown reporting
cannot race a detached reply processor.

Validation:
- 38/38 patches cleanly apply in SERIES order and reproduce the audited effective source byte-for-byte.
- dedicated diagnostic stats concurrency test PASS under strict, ASan+UBSan and TSan.
- 36/36 strict C tests PASS.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`.
- CodeGraph effective tree: 16 files / 444 nodes / 1,284 edges.

## CP31-R — RTT average uses completed replies as its denominator

`total_rtt` is accumulated only when replies are processed, but the shutdown
summary divided it by `num_sent`. A disconnect or shutdown with outstanding
requests therefore understated the reported average RTT. The denominator now
uses `num_received`, the population that actually contributed to `total_rtt`.

Validation:
- 39/39 patches cleanly apply and reproduce the effective source.
- dedicated RTT average contract PASS.
- 37/37 strict C tests PASS.
- patch-series contract PASS.

## CP31-S — hard-link aliases inherit connection affinity

With multiple SFTP connections, `conntab` keeps pathname operations on the
connection that owns an already-open file, preventing a later path operation
from bypassing pending asynchronous writes. A successful hard link creates a
second pathname for the same inode, but the old implementation left the new
pathname without the source's connection affinity. `write(from)` followed by
`link(from, to)` could therefore route a later `getattr/open/read(to)` through a
different connection and escape the ordering model.

After a successful hard-link extension request, `to` now points at the same
`conntab_entry` as `from` when a source affinity exists. Pathname aliases do not
increment the entry refcount; open file objects remain the owners. If the source
has no affinity, any stale destination pathname mapping is removed so an older
unlinked inode cannot donate its connection to the newly-created link.

Validation:
- 40/40 patches cleanly apply in SERIES order and reproduce the audited effective `sshfs.c` byte-for-byte.
- 38/38 strict C tests PASS.
- dedicated hardlink-affinity contract PASS under strict and ASan+UBSan.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; BOM scan: zero files.
- CodeGraph effective tree: 16 files / 444 nodes / 1,284 edges; impact of `sshfs_link` is local to `sshfs.c`.

## CP31-T — pathname affinity follows namespace lifetime

Connection affinity keys now obey the lifetime of the pathname that names an
inode. A successful `unlink(path)` removes that pathname key while leaving any
open-file-owned `conntab_entry` alive through its refcount. Successful rename
also discards a stale destination affinity when the source has no entry.

CP31-S additionally makes known hard-link aliases visible because both keys
point at the same entry. When the POSIX rename extension succeeds and `from`
and `to` already share that entry, rename is an inode-identity no-op and both
alias keys are preserved. Normal rename continues to move the source affinity
to the destination.

Validation:
- 41/41 patches cleanly apply in SERIES order and reproduce the audited effective `sshfs.c` byte-for-byte.
- 39/39 strict C tests PASS.
- dedicated pathname-affinity lifecycle contract PASS under strict and ASan+UBSan.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; BOM scan: zero files.
- CodeGraph impact is limited to `sshfs_unlink`, its rename-workaround caller, and `sshfs_rename`.

## CP31-U — synchronous write commit barriers

- Safe (`sshfs_sync`) writes now wait for each SFTP WRITE STATUS before sending the next chunk.
- A failure after a committed prefix returns a short write rather than an all-or-error result.
- Later chunks are never sent after the first failed chunk, which prevents APPEND retries from duplicating already committed data.
- The synchronous path now reuses the normal request-wait STATUS validator instead of maintaining a parallel callback/condition-variable completion mechanism.
- Added `tests/sync-write-commit-contract-test.c`.

## CP31-V — non-destructive VSOCK reconnect parsing

- `connect_vsock()` no longer overwrites the persistent `CID:PORT` option string.
- Reconnect can parse the same VSOCK endpoint repeatedly.
- CID and PORT must be non-empty decimal unsigned values with no trailing junk and must fit in `unsigned int`.
- Added `tests/vsock-parser-contract-test.c`, including a same-buffer second-parse reconnect check.

## CP31-W — directport IPv4/IPv6 address fallback

- `connect_to()` now asks `getaddrinfo()` for `AF_UNSPEC` instead of forcing hostnames to IPv4 and colon-containing literals to IPv6.
- All resolved address candidates are tried in resolver order; a failed first address no longer makes the whole direct connection fail.
- Socket creation failures also fall through to later candidates.
- Added `tests/directport-address-fallback-test.c`.

## CP31-X — release CLOSE completion barrier

- File and directory release now wait for the SFTP CLOSE `SSH_FXP_STATUS` reply on a live generation.
- Reply-side remote-handle accounting therefore retires the handle before release returns.
- This prevents a close-then-open sequence from spuriously hitting `handle_limit` while the CLOSE acknowledgement is still in flight.
- CLOSE protocol/remote errors now propagate from release instead of being unobserved protocol-level best effort.
- Added `tests/release-close-handle-accounting-test.c`.

## CP31-Y — POSIX/FUSE utimens special-time semantics

- Unix epoch (`tv_sec == 0`) is now preserved as timestamp 0 instead of being rewritten to current time.
- `UTIME_NOW` is resolved from a single `CLOCK_REALTIME` snapshot.
- `UTIME_OMIT` preserves the corresponding remote timestamp; when one side is omitted, current attributes are fetched first because SFTP v3 transmits atime/mtime as a pair.
- Two `UTIME_OMIT` values are a network no-op.
- Invalid nanoseconds return `EINVAL`; timestamps outside SFTP v3 unsigned 32-bit seconds return `ERANGE` instead of silently wrapping.
- Added `tests/utimens-contract-test.c`.

## CP31-Z — truncate workaround preservation boundary

The shrink workaround used a local zero-filled buffer, read the target prefix,
then reopened the remote file with `O_TRUNC` and wrote the buffer back. A
premature EOF before the requested target size therefore left the unread tail
as zeroes yet still crossed the destructive `O_TRUNC` boundary. Concurrent
remote mutation or an unexpected short source could silently replace real data
with zeroes.

The workaround now treats complete preservation as a precondition for any
truncating reopen. Zero-byte reads and writes are `EIO` rather than successful
progress, preservation-handle release errors abort before truncation, rewrite
and zero/extend helper release errors propagate, and chunk sizing uses remaining
bytes rather than `offset + max_read` so the calculation cannot overflow.
Negative sizes are rejected before destructive work and shrink sizes that do
not fit `size_t` fail with `EOVERFLOW`.

Validation:
- 47/47 patches cleanly apply in SERIES order and reproduce the audited effective `sshfs.c` byte-for-byte.
- 45/45 strict C tests PASS.
- 45/45 ASan+UBSan C tests PASS.
- dedicated truncate preservation contract PASS.
- Windows tooling, patch-series and distribution-signing contracts PASS.
- all shell scripts pass `bash -n`; wrapper strict compile PASS; BOM scan: zero files.
- CodeGraph effective tree: 16 files / 443 nodes / 1,282 edges; impact of `sshfs_truncate_shrink` is limited to the truncate workaround/caller chain.

## CP32-A — self-contained source artifact and finishing hygiene

The checkpoint archive no longer assumes nested SSHFS Git metadata that it does
not ship. Bundled pristine SSHFS is staged as a plain directory and verified by
reconstructing the pinned Git tree identity before patch application. The lost
upstream executable mode on `sshfs/test/test_sshfs.py` was restored so the
source tree matches the pinned upstream tree exactly. `make clean` no longer
requires a top-level Git checkout. Generated checkpoint test-bin/log residue is
removed and guarded by an artifact-hygiene contract.
