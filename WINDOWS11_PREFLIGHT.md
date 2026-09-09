# Windows 11 preflight and Error 67 classification — current checkpoint

`tools/diagnose-windows11.ps1` collects the state needed to distinguish:

- Network Provider registration failure;
- stopped/missing WinFsp Launcher;
- stale SSHFS launcher class registrations;
- launcher user-context mismatch (`RunAs != "."`);
- missing bundled binaries;
- user SSH configuration visibility;
- per-image exploit-protection / ASLR configuration.

The optional `-ProbeNetworkProvider -UncPath ...` mode executes only the normal
`net use` path and removes a successful test mapping. It deliberately does not
fall back to launching `sshfs-win.exe svc` automatically, so diagnostics do not
silently bypass the component under test.

The script now exits nonzero when a required preflight check fails and writes its report as UTF-8 without BOM. Missing optional capabilities are reported as unavailable rather than being conflated with command failure.

`-SshHost <host-or-alias>` asks the bundled OpenSSH client to expand its effective configuration with `ssh -G -o BatchMode=yes`. This validates config syntax without making a network connection and records the effective key/known-hosts/agent/timeout policy that Explorer launcher mounts will inherit.

Launcher registration reads use the Windows Registry32 view explicitly, matching WinFsp's service-registration view without depending on PowerShell process bitness. The optional drive probe refuses to touch an already-used drive letter and removes only a mapping that the probe itself successfully created.
