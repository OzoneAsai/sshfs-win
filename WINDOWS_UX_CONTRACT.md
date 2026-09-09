# Windows integration and UX contract — current checkpoint

SSHFS-Win is surfaced through Windows Explorer, `net use`, and the WinFsp
Network Provider/Launcher. Those components are the user interface. The
wrapper, SSHFS core, OpenSSH handoff, diagnostics, and packaging therefore use
one failure model instead of translating every failure into “mount failed”.

## State model

For configuration and discovery there are four states:

1. **absent** — an optional value is not configured; use the documented default;
2. **present and valid** — use the configured value;
3. **present but invalid** — reject it and name the offending setting;
4. **unreadable/failed** — report the operation that failed; never reinterpret
   it as “absent”.

Cleanup errors are reported, but do not overwrite an earlier primary failure.

## Launcher registry boundary

Launcher configuration tools use the Windows Registry32 view explicitly. Mutations require administrator rights, use one shared implementation, and are verified by reading the value back before success is reported.

## Launcher and argument boundary

- `svc` starts recover the original Launcher UTF-16 command line from
  `kernel32.dll`, convert it strictly to UTF-8, and parse Windows quoting.
- A recovery failure records its exact stage and Win32 error before falling
  back to Cygwin argv.
- An argc mismatch is observable and also falls back rather than mixing two
  argument vectors.
- Empty remote/alias and empty `locuser=` inputs are rejected at the wrapper
  boundary.
- Port values are decimal `1..65535`; malformed values are rejected before
  OpenSSH is started.

## Authentication boundary

Password launcher classes use WinFsp's stdin/stdout credential bridge and keep
the eager connection required by that protocol.

Public-key launcher classes are deliberately non-interactive:
`BatchMode=yes` is passed to OpenSSH. A missing key, unavailable authentication
agent, or passphrase-protected key without a usable agent therefore fails
promptly instead of creating an invisible prompt behind Explorer.

Host-key policy stays in normal OpenSSH configuration. The packaged system
configuration uses `StrictHostKeyChecking accept-new`, and user configuration
can choose a stricter policy.

## Process and thread startup

A successful process handoff is observable as execution replacing the wrapper.
If `execve` itself fails, wrapper command/service modes use conventional
execution failure codes:

- `127`: executable not found;
- `126`: executable found but could not be executed.

The legacy SSH double-fork path has a close-on-exec status channel. EOF means
the grandchild reached `exec`; `chdir`, `setsid`, PTY, second-fork, and
`execvp` failures are returned to the parent with their stage and `errno`.

Request-processing thread creation is transactional with respect to the
caller's signal mask. FUSE initialization exits with failure if its eager
request processor cannot start.

## Diagnostics

Diagnostics are opt-in and persistent logging is disabled by default.

The Windows 11 diagnostic script distinguishes missing registry values from
registry read failures, checks the bundled SSH executable's real exit status,
records optional platform-capability availability separately, and exits
nonzero when a required preflight check fails. Generated text and CSV files
are UTF-8 without BOM.

## Build and release

A release artifact is not considered complete after a failed signing step.
Signing failure is fatal by default. Deliberate local unsigned builds require
the explicit `AllowUnsigned=1` opt-in.

Runtime-closure and dependency-manifest generation fail if provenance or
version discovery fails; “unknown” is not substituted for a failed required
probe.

## Resulting Windows UX

The intended user-visible progression is:

`Windows request -> Launcher -> wrapper validation -> authentication mode ->
SSHFS/OpenSSH handoff -> mounted filesystem`

A failure is reported at the first boundary that cannot complete. Later layers
are not started merely to obtain a different error message, and earlier-layer
failures are not reclassified as missing optional state.
