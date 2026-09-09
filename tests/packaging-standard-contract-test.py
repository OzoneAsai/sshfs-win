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
    "WixProject = $(PrjDir)/sshfs-win.wixproj",
    'MyVersion = $(strip $(shell cat "$(VersionFile)"))',
    "$(Status)/wix: $(Status)/sshfs-win sshfs-win.wxs sshfs-win.wixproj $(VersionFile)",
    "dotnet build",
    "--property:InstallerPlatform=$(MyArch)",
    "--property:RootDir=",
    'MyCompanyName = "OzoneAsai"',
    "SigningSubject ?= $(MyCompanyName)",
    "HostArch := $(shell uname -m)",
    "$(error Unsupported build host architecture",
):
    if token not in makefile:
        failures.append(f"Makefile: missing packaging identity/toolchain contract {token}")
for forbidden in ("candle ", "heat dir", "light ", "WixUIExtension"):
    if forbidden in makefile:
        failures.append(f"Makefile: legacy WiX v3 pipeline still present: {forbidden!r}")
if 'MyCompanyName = "Navimatics LLC"' in makefile:
    failures.append("Makefile: fork package still claims the upstream manufacturer identity")

wixproj_path = root / "sshfs-win.wixproj"
if not wixproj_path.is_file():
    failures.append("sshfs-win.wixproj: missing WiX v7 SDK project")
    wixproj = ""
else:
    wixproj = wixproj_path.read_text(encoding="utf-8")
for token in (
    'Sdk="WixToolset.Sdk/7.0.0"',
    "<AcceptEula>wix7</AcceptEula>",
    "<InstallerPlatform",
    "<DefineConstants>",
    'PackageReference Include="WixToolset.UI.wixext" Version="7.0.0"',
):
    if token not in wixproj:
        failures.append(f"sshfs-win.wixproj: missing WiX v7 contract {token}")

apply = (root / "tools" / "apply-checkpoint.sh").read_text(encoding="utf-8")
for token in (
    'cp "$SELF_DIR/VERSION" "$REPO/VERSION"',
    'cp -a "$SELF_DIR/sshfs" "$REPO/sshfs"',
    'verify-sshfs-source.sh" --source-only "$REPO/sshfs"',
    'rm -f -- "$REPO/.gitmodules"',
    'cp "$SELF_DIR/README.md" "$REPO/README.md"',
    'cp "$SELF_DIR/sshfs-win.wixproj" "$REPO/sshfs-win.wixproj"',
    'cp "$SELF_DIR/.github/workflows/contracts.yml" "$REPO/.github/workflows/contracts.yml"',
    'rm -f -- "$REPO/GroupReadWrite.reg" "$REPO/ServerAliveInterval.reg"',
):
    if token not in apply:
        failures.append(f"apply-checkpoint.sh: missing self-contained transformation contract {token}")
for forbidden in (
    'git -C "$REPO/sshfs" cat-file',
    'git -C "$REPO/sshfs" checkout',
    'git -C "$REPO/sshfs" status',
    "submodule object store",
):
    if forbidden in apply:
        failures.append(f"apply-checkpoint.sh: still depends on target SSHFS Git metadata: {forbidden}")

wxs = (root / "sshfs-win.wxs").read_text(encoding="utf-8")
for token in (
    'xmlns="http://wixtoolset.org/schemas/v4/wxs"',
    "<Package",
    'InstallerVersion="500"',
    '<StandardDirectory Id="ProgramFiles6432Folder">',
    '<Files Include="$(RootDir)\\**" Directory="INSTALLDIR" />',
    'AllowSameVersionUpgrades="yes"',
    'Value="Software\\WinFsp\\Services"',
):
    if token not in wxs:
        failures.append(f"sshfs-win.wxs: missing WiX v7 authoring contract {token}")
for forbidden in (
    "http://schemas.microsoft.com/wix/2006/wi",
    'Win64="no"',
    "WOW6432Node",
    "<ComponentGroupRef Id=\"C.Main\"",
):
    if forbidden in wxs:
        failures.append(f"sshfs-win.wxs: legacy WiX v3 authoring remains: {forbidden}")
if re.search(r"<Product(?:\s|>)", wxs):
    failures.append("sshfs-win.wxs: legacy WiX v3 Product element remains")
for component in ("C.sshfs.reg", "C.sshfs.r.reg", "C.sshfs.k.reg", "C.sshfs.kr.reg"):
    pattern = rf'<Component\s+Id="{re.escape(component)}"[^>]*\bBitness="always32"'
    if re.search(pattern, wxs) is None:
        failures.append(f"sshfs-win.wxs: {component} does not explicitly target the 32-bit registry view")

if (root / ".gitmodules").exists():
    failures.append(".gitmodules: stale sshfs submodule metadata still present beside vendored source")

readme = (root / "README.md").read_text(encoding="utf-8")
for token in (
    "development fork",
    "not this fork",
    "vendored, pinned pristine SSHFS source directory",
    "WiX v7",
):
    if token not in readme:
        failures.append(f"README.md: missing fork/build boundary text {token!r}")

if failures:
    print("\n".join(f"FAIL {failure}" for failure in failures), file=sys.stderr)
    raise SystemExit(1)

print(f"Packaging standard contract: PASS ({version}, WiX v7)")
