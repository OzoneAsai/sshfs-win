#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
failures = []

version_path = root / "VERSION"
if not version_path.is_file():
    failures.append("VERSION: missing deterministic package version source")
    version = ""
else:
    version = version_path.read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", version)
    if match is None:
        failures.append(f"VERSION: expected numeric major.minor.build, got {version!r}")
    else:
        major, minor, build = map(int, match.groups())
        if major > 255 or minor > 255 or build > 65535:
            failures.append("VERSION: exceeds Windows Installer ProductVersion field limits")

makefile = (root / "Makefile").read_text(encoding="utf-8")
if "date '+%y%j'" in makefile or "shell date" in makefile:
    failures.append("Makefile: package identity still depends on wall-clock time")
for token in (
    "VersionFile = $(PrjDir)/VERSION",
    'MyVersion = $(strip $(shell cat "$(VersionFile)"))',
    "$(Status)/wix: $(Status)/sshfs-win sshfs-win.wxs $(VersionFile)",
):
    if token not in makefile:
        failures.append(f"Makefile: missing deterministic-version contract {token}")

apply = (root / "tools" / "apply-checkpoint.sh").read_text(encoding="utf-8")
if 'cp "$SELF_DIR/VERSION" "$REPO/VERSION"' not in apply:
    failures.append("apply-checkpoint.sh: VERSION is not propagated with the checkpoint")

wxs = (root / "sshfs-win.wxs").read_text(encoding="utf-8")
if "WOW6432Node" in wxs:
    failures.append("sshfs-win.wxs: physical WOW6432Node path used instead of MSI registry view")
if '<?define LauncherRegistryKey="Software\\WinFsp\\Services"?>' not in wxs:
    failures.append("sshfs-win.wxs: launcher key is not expressed as the logical registry path")
if 'Disallow="yes"' in wxs:
    failures.append("sshfs-win.wxs: MajorUpgrade still blocks normal upgrades")
if 'AllowSameVersionUpgrades="yes"' not in wxs:
    failures.append("sshfs-win.wxs: same-version rebuilds are not recognized as upgrades")

for component in ("C.sshfs.reg", "C.sshfs.r.reg", "C.sshfs.k.reg", "C.sshfs.kr.reg"):
    pattern = rf'<Component\s+Id="{re.escape(component)}"[^>]*\bWin64="no"'
    if re.search(pattern, wxs) is None:
        failures.append(f"sshfs-win.wxs: {component} does not explicitly target the 32-bit registry view")

if (root / ".gitmodules").exists():
    failures.append(".gitmodules: stale sshfs submodule metadata still present beside vendored source")

if failures:
    print("\n".join(f"FAIL {failure}" for failure in failures), file=sys.stderr)
    raise SystemExit(1)

print(f"Packaging standard contract: PASS ({version})")
