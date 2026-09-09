#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
tools = root / "tools"
helper = (tools / "launcher-registry.ps1").read_text(encoding="utf-8")

required = (
    "Invoke-SshfsWinLauncherValueTransaction",
    "$snapshots = @{}",
    "$modified = [System.Collections.Generic.List[string]]::new()",
    "for ($i = $modified.Count - 1; $i -ge 0; $i--)",
    "$snapshot.Kind",
    "rolled back",
    "Set-SshfsWinLauncherCommandLineOptions",
    "$targets = @{}",
    "preserving every unrelated option",
)
for token in required:
    if token not in helper:
        raise SystemExit(f"launcher-registry.ps1 missing transaction contract token: {token}")

for name in ("set-server-alive-interval.ps1", "set-group-read-write.ps1"):
    text = (tools / name).read_text(encoding="utf-8")
    if "Set-SshfsWinLauncherCommandLineOptions" not in text:
        raise SystemExit(f"{name} must merge SSHFS options through the shared helper")
    if "Set-SshfsWinLauncherString" in text or "-Name 'CommandLine'" in text:
        raise SystemExit(f"{name} must not replace CommandLine wholesale")

keepalive = (tools / "set-server-alive-interval.ps1").read_text(encoding="utf-8")
if "ServerAliveInterval = 30" not in keepalive:
    raise SystemExit("set-server-alive-interval.ps1 lost its intended keepalive value")

umask = (tools / "set-group-read-write.ps1").read_text(encoding="utf-8")
for token in ("create_file_umask = '0117'", "create_dir_umask = '0007'"):
    if token not in umask:
        raise SystemExit(f"set-group-read-write.ps1 missing {token}")

print("Launcher registry transaction contract: PASS")
