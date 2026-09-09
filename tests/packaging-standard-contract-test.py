#!/usr/bin/env python3
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
failures = []


def fail(message: str) -> None:
    failures.append(message)


version_path = root / "VERSION"
if not version_path.is_file():
    fail("VERSION: missing deterministic package version source")
    version = ""
else:
    version = version_path.read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)", version)
    if match is None:
        fail(f"VERSION: expected numeric major.minor.build, got {version!r}")
    else:
        major, minor, build = map(int, match.groups())
        if major > 255 or minor > 255 or build > 65535:
            fail("VERSION: exceeds Windows Installer ProductVersion field limits")

makefile = (root / "Makefile").read_text(encoding="utf-8")
if "date '+%y%j'" in makefile or "shell date" in makefile:
    fail("Makefile: package identity still depends on wall-clock time")
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
        fail(f"Makefile: missing packaging identity/toolchain contract {token}")
for forbidden in ("candle ", "heat dir", "light ", "WixUIExtension"):
    if forbidden in makefile:
        fail(f"Makefile: legacy WiX v3 pipeline still present: {forbidden!r}")
if 'MyCompanyName = "Navimatics LLC"' in makefile:
    fail("Makefile: fork package still claims the upstream manufacturer identity")

wixproj_path = root / "sshfs-win.wixproj"
try:
    wixproj_root = ET.parse(wixproj_path).getroot()
except (OSError, ET.ParseError) as exc:
    fail(f"sshfs-win.wixproj: cannot parse WiX SDK project: {exc}")
    wixproj_root = None

if wixproj_root is not None:
    if wixproj_root.tag != "Project" or wixproj_root.get("Sdk") != "WixToolset.Sdk/7.0.0":
        fail("sshfs-win.wixproj: must use WixToolset.Sdk/7.0.0")
    properties = {}
    for group in wixproj_root.findall("PropertyGroup"):
        for child in group:
            properties.setdefault(child.tag, []).append((child.text or "").strip())
    if "wix7" not in properties.get("AcceptEula", []):
        fail("sshfs-win.wixproj: WiX v7 EULA acceptance is not explicit")
    if not properties.get("InstallerPlatform"):
        fail("sshfs-win.wixproj: InstallerPlatform is not modeled")
    if not properties.get("DefineConstants"):
        fail("sshfs-win.wixproj: MSBuild-to-WiX constant boundary is missing")
    ui_refs = [
        item for group in wixproj_root.findall("ItemGroup")
        for item in group.findall("PackageReference")
        if item.get("Include") == "WixToolset.UI.wixext"
    ]
    if not ui_refs or any(item.get("Version") != "7.0.0" for item in ui_refs):
        fail("sshfs-win.wixproj: WixToolset.UI.wixext must be pinned to 7.0.0")

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
        fail(f"apply-checkpoint.sh: missing self-contained transformation contract {token}")
for forbidden in (
    'git -C "$REPO/sshfs" cat-file',
    'git -C "$REPO/sshfs" checkout',
    'git -C "$REPO/sshfs" status',
    "submodule object store",
):
    if forbidden in apply:
        fail(f"apply-checkpoint.sh: still depends on target SSHFS Git metadata: {forbidden}")

WXS_NS = "http://wixtoolset.org/schemas/v4/wxs"
UI_NS = "http://wixtoolset.org/schemas/v4/wxs/ui"
W = f"{{{WXS_NS}}}"
U = f"{{{UI_NS}}}"
wxs_path = root / "sshfs-win.wxs"
try:
    wxs_root = ET.parse(wxs_path).getroot()
except (OSError, ET.ParseError) as exc:
    fail(f"sshfs-win.wxs: cannot parse WiX v7 authoring: {exc}")
    wxs_root = None

if wxs_root is not None:
    if wxs_root.tag != f"{W}Wix":
        fail("sshfs-win.wxs: root namespace is not the WiX v4/v7 schema")
    package = wxs_root.find(f"{W}Package")
    if package is None:
        fail("sshfs-win.wxs: Package element missing")
    else:
        if package.get("InstallerVersion") != "500":
            fail("sshfs-win.wxs: InstallerVersion must target modern Windows Installer")
        if not package.get("UpgradeCode"):
            fail("sshfs-win.wxs: package UpgradeCode is missing")

        major = package.find(f"{W}MajorUpgrade")
        if major is None:
            fail("sshfs-win.wxs: MajorUpgrade model missing")
        elif major.get("AllowSameVersionUpgrades") == "yes":
            fail("sshfs-win.wxs: same-version major upgrades violate the normal MSI version model")

        install_root = next(
            (d for d in package.findall(f"{W}StandardDirectory")
             if d.get("Id") == "ProgramFiles6432Folder"),
            None,
        )
        if install_root is None:
            fail("sshfs-win.wxs: ProgramFiles6432Folder architecture model missing")
        elif not any(d.get("Id") == "INSTALLDIR" for d in install_root.findall(f"{W}Directory")):
            fail("sshfs-win.wxs: INSTALLDIR is not rooted under ProgramFiles6432Folder")

        launcher_property = next(
            (p for p in package.findall(f"{W}Property")
             if p.get("Id") == "P.LauncherRegistryKey"),
            None,
        )
        if launcher_property is None or launcher_property.get("Value") != r"Software\WinFsp\Services":
            fail("sshfs-win.wxs: launcher registry key must use the logical registry path")

        for component_id in ("C.sshfs.reg", "C.sshfs.r.reg", "C.sshfs.k.reg", "C.sshfs.kr.reg"):
            component = next(
                (c for c in package.findall(f"{W}Component") if c.get("Id") == component_id),
                None,
            )
            if component is None:
                fail(f"sshfs-win.wxs: missing launcher component {component_id}")
                continue
            if component.get("Bitness") != "always32":
                fail(f"sshfs-win.wxs: {component_id} must explicitly target Registry32")
            if component.get("Directory") != "TARGETDIR":
                fail(f"sshfs-win.wxs: {component_id} must not inherit ProgramFiles package bitness")

        files = package.findall(f".//{W}Files")
        if not any(f.get("Include") == r"$(RootDir)\**" and f.get("Directory") == "INSTALLDIR" for f in files):
            fail("sshfs-win.wxs: runtime closure is not harvested by the WiX v7 Files model")

        wix_ui = package.find(f"{U}WixUI")
        if wix_ui is None or wix_ui.get("Id") != "WixUI_FeatureTree":
            fail("sshfs-win.wxs: standard UI must be referenced through WixToolset.UI.wixext")
        if package.find(f".//{W}UIRef") is not None:
            fail("sshfs-win.wxs: legacy v3 UIRef remains")

if (root / ".gitmodules").exists():
    fail(".gitmodules: stale sshfs submodule metadata still present beside vendored source")
for legacy_reg in ("GroupReadWrite.reg", "ServerAliveInterval.reg"):
    if (root / legacy_reg).exists():
        fail(f"{legacy_reg}: physical registry-view mutation file must not return")

readme = (root / "README.md").read_text(encoding="utf-8")
for token in (
    "development fork",
    "not this fork",
    "vendored, pinned pristine SSHFS source directory",
    "WiX v7",
):
    if token not in readme:
        fail(f"README.md: missing fork/build boundary text {token!r}")

if failures:
    print("\n".join(f"FAIL {failure}" for failure in failures), file=sys.stderr)
    raise SystemExit(1)

print(f"Packaging standard contract: PASS ({version}, WiX v7)")
