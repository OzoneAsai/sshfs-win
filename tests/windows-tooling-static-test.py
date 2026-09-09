#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
tools = root / "tools"

failures = []

mutation_scripts = [
    "enable-reconnect.ps1", "disable-reconnect.ps1",
    "enable-delay-connect.ps1", "disable-delay-connect.ps1",
    "enable-safe-writes.ps1", "disable-safe-writes.ps1",
    "enable-parallel-connections.ps1", "disable-parallel-connections.ps1",
    "enable-stall-observer.ps1", "disable-stall-observer.ps1",
    "enable-handle-observer.ps1", "disable-handle-observer.ps1",
    "enable-handle-limit.ps1", "disable-handle-limit.ps1",
    "enable-diagnostics.ps1", "disable-diagnostics.ps1",
    "set-server-alive-interval.ps1", "set-group-read-write.ps1",
]

for name in mutation_scripts:
    text = (tools / name).read_text(encoding="utf-8")
    if "launcher-registry.ps1" not in text:
        failures.append(f"{name}: does not use shared launcher registry layer")
    if "Assert-SshfsWinAdministrator" not in text:
        failures.append(f"{name}: does not fail early when PowerShell is not elevated")
    if "HKLM:\\SOFTWARE\\WinFsp\\Services" in text or "WOW6432Node\\WinFsp\\Services" in text:
        failures.append(f"{name}: hard-codes launcher registry view")

for p in tools.glob("*.ps1"):
    data = p.read_bytes()
    text = data.decode("utf-8")
    if "SilentlyContinue" in text or "-ErrorAction Ignore" in text:
        failures.append(f"{p.name}: suppresses PowerShell errors")

for pattern in ("*.ps1", "*.py", "*.sh", "*.reg", "*.md", "*.c", "*.h", "*.inc", "*.wxs"):
    for p in root.rglob(pattern):
        data = p.read_bytes()
        if data.startswith((b"\xef\xbb\xbf", b"\xff\xfe", b"\xfe\xff")):
            failures.append(f"{p.relative_to(root)}: BOM present")

helper = (tools / "launcher-registry.ps1").read_text(encoding="utf-8")
if "WOW6432Node" in helper:
    failures.append("launcher-registry.ps1: hard-codes WOW6432Node instead of using Registry32 view")

for token in (
    "RegistryView]::Registry32",
    "RegistryKey]::OpenBaseKey",
    ".SetValue(",
    ".GetValue(",
    ".DeleteValue(",
    "Get-SshfsWinLauncherValueResult",
    "Assert-SshfsWinAdministrator",
):
    if token not in helper:
        failures.append(f"launcher-registry.ps1: missing {token}")

diag = (tools / "diagnose-windows11.ps1").read_text(encoding="utf-8")
for token in (
    "function Invoke-CapturedProcess",
    "RedirectStandardOutput",
    "RedirectStandardError",
):
    if token not in diag:
        failures.append(f"diagnose-windows11.ps1: missing native-process capture contract {token}")
if "-ArgumentList @()" in diag:
    failures.append("diagnose-windows11.ps1: passes an empty ArgumentList to Start-Process")
if "& $sshExe -V" in diag or "& $sshExe -G" in diag:
    failures.append("diagnose-windows11.ps1: invokes bundled ssh through PowerShell native stderr semantics")
for token in (
    "WinFsp file-system driver is demand-start",
    "service is not running",
    "RunAs must be '.'",
    "is incompatible with the password credential bridge",
    "sshfs.delay_connect must be 0 for password launcher classes",
    "Result: FAIL",
    "UTF8Encoding]::new($false)",
    "is already in use; choose a free drive letter for the probe",
):
    if token not in diag:
        failures.append(f"diagnose-windows11.ps1: missing contract check {token}")

if 'pre-cleanup-exit' in diag or 'net use $Drive /delete /y" 2>&1\n        $preCleanup' in diag:
    failures.append("diagnose-windows11.ps1: probe must not delete a pre-existing drive mapping")

apply = (tools / "apply-checkpoint.sh").read_text(encoding="utf-8")
if 'cp -p "$SELF_DIR"/tools/* "$REPO/tools/"' not in apply:
    failures.append("apply-checkpoint.sh: must apply the complete checkpoint-owned tools directory")
if 'cp "$SELF_DIR/portable-format.h" "$REPO/portable-format.h"' not in apply:
    failures.append("apply-checkpoint.sh: must carry portable-format.h with sshfs-win.c")
if 'cp "$SELF_DIR/windows-commandline-parser.h" "$REPO/windows-commandline-parser.h"' not in apply:
    failures.append("apply-checkpoint.sh: must carry the shared Windows command-line parser")
if '"$SELF_DIR/tools/stage-sshfs-source.sh" "$SELF_DIR/sshfs" "$REPO/sshfs"' not in apply:
    failures.append("apply-checkpoint.sh: must use verified portable SSHFS source staging")
if "-name '*.py'" not in apply or "-name '*.sh'" not in apply:
    failures.append("apply-checkpoint.sh: must carry checkpoint-owned shell/Python tests")
if "top_level_patches=(" not in apply or "shopt -s nullglob" not in apply:
    failures.append("apply-checkpoint.sh: optional top-level patch set must not expand to a literal glob")

parallel = (tools / "enable-parallel-connections.ps1").read_text(encoding="utf-8")
if "-Classes @('sshfs.k', 'sshfs.kr')" not in parallel:
    failures.append("enable-parallel-connections.ps1: must only modify public-key classes")

wrapper = (root / "sshfs-win.c").read_text(encoding="utf-8")
if "asprintf(" in wrapper:
    failures.append("sshfs-win.c: depends on asprintf instead of the shared portable formatter")
if '#include "portable-format.h"' not in wrapper:
    failures.append("sshfs-win.c: missing portable-format.h")
if '#include "windows-commandline-parser.h"' not in wrapper:
    failures.append("sshfs-win.c: does not use the shared production Windows argv parser")
if "static int win32_parse_command_line_utf8" in wrapper:
    failures.append("sshfs-win.c: duplicates the shared Windows argv parser implementation")
for name in ("stallopt", "handleopt", "limitopt", "maxconnopt"):
    if f"char {name}[64];" in wrapper:
        failures.append(f"sshfs-win.c: {name} has block lifetime but is retained in exec argv")

parser = (root / "windows-commandline-parser.h").read_text(encoding="utf-8")
for token, description in (
    ("0 == backslashes && quoted && '\"' == src[1]", "Microsoft CRT doubled-quote rule"),
    ("if (!quoted && (' ' == *src || '\\t' == *src))", "post-backslash unquoted-whitespace delimiter rule"),
):
    if token not in parser:
        failures.append(f"windows-commandline-parser.h: missing {description}")
if (root / "tests" / "windows-native-argv-parser.inc").exists():
    failures.append("tests/windows-native-argv-parser.inc: duplicate parser fixture must not exist")
argv_test = (root / "tests" / "windows-native-argv-test.c").read_text(encoding="utf-8")
if '#include "../windows-commandline-parser.h"' not in argv_test:
    failures.append("windows-native-argv-test.c: must test the shared production parser")

cp30_patch = (root / "patches" / "90-concurrency-and-write-contract.patch").read_text(encoding="utf-8")
for token in ("get_conn_locked", "sshfs_write_reply_error", "write_error = sf->write_error"):
    if token not in cp30_patch:
        failures.append(f"90-concurrency-and-write-contract.patch: missing {token}")

vendor_common = (tools / "vendor-ssh-common.sh").read_text(encoding="utf-8")
for token in (
    'OPENSSH_BUILD_TARGET=ssh.exe',
    'vendor_ssh_lock_field "OpenSSH portable" version',
    'make -r -j"$jobs" "$OPENSSH_BUILD_TARGET"',
    'PATH="$prefix/bin:/usr/local/bin:/usr/bin" "$ssh_exe" -V',
):
    if token not in vendor_common:
        failures.append(f"vendor-ssh-common.sh: missing {token}")

for name in ("build-vendor-ssh.sh", "continue-vendor-ssh.sh", "finalize-vendor-ssh.sh"):
    text = (tools / name).read_text(encoding="utf-8")
    if "vendor-ssh-common.sh" not in text:
        failures.append(f"{name}: does not use shared vendor SSH model")
    if 'make -j"$JOBS" ssh' in text or 'make -j"${NUMBER_OF_PROCESSORS:-4}" ssh' in text:
        failures.append(f"{name}: contains legacy implicit OpenSSH target")

for name in ("GroupReadWrite.reg", "ServerAliveInterval.reg"):
    if (root / name).exists():
        failures.append(f"{name}: legacy .reg mutation bypasses the explicit Registry32 helper layer")

repair = (tools / "repair-test-registration.ps1").read_text(encoding="utf-8")
for token in ("RegistryView]::Registry64", "RegistryView]::Registry32"):
    if token not in repair:
        failures.append(f"repair-test-registration.ps1: missing explicit {token}")
if "WOW6432Node" in repair:
    failures.append("repair-test-registration.ps1: physical WOW6432Node path used instead of registry views")

stager = (tools / "stage-sshfs-source.sh").read_text(encoding="utf-8")
for token in (
    'verify-sshfs-source.sh" --source-only "$SRC"',
    'cp -R -- "$SRC" "$DST"',
    'verify-sshfs-source.sh" --source-only "$DST"',
):
    if token not in stager:
        failures.append(f"stage-sshfs-source.sh: missing verified staging contract {token}")
if "cp -a" in stager:
    failures.append("stage-sshfs-source.sh: must not preserve Cygwin/NTFS directory ACL metadata with cp -a")

makefile = (root / "Makefile").read_text(encoding="utf-8")
if "meson setup .." not in makefile or "\n\t\tmeson .." in makefile:
    failures.append("Makefile: must use explicit 'meson setup' command")
if "$(Status)/sshfs-win: $(Status)/root sshfs-win.c portable-format.h windows-commandline-parser.h" not in makefile:
    failures.append("Makefile: wrapper target must depend on both shared headers")
if "git -c core.autocrlf=false clone $(PrjDir)/sshfs" in makefile:
    failures.append("Makefile: bundled SSHFS staging must not require nested .git metadata")
if 'tools/stage-sshfs-source.sh "$(PrjDir)/sshfs" "$(SrcDir)/sshfs"' not in makefile:
    failures.append("Makefile: bundled SSHFS source must use the verified portable staging helper")
if 'cp -a "$(PrjDir)/sshfs"' in makefile:
    failures.append("Makefile: must not use cp -a for Cygwin/NTFS SSHFS staging")
if "git clean -dffx" in makefile or 'rm -rf -- "$(PrjDir)/.build"' not in makefile:
    failures.append("Makefile: clean target must work from a source archive without git metadata")
verifier = (tools / "verify-sshfs-source.sh").read_text(encoding="utf-8")
for token in (
    "EXPECTED_TREE=35655f60d37403663238a73b4204cfd64fdca73c",
    'GIT_WORK_TREE="$SRC" git -C "$meta" add -A',
    'tree=$(git -C "$meta" write-tree)',
):
    if token not in verifier:
        failures.append(f"verify-sshfs-source.sh: missing plain-source tree verification token {token}")

checkpoint = (root / "CHECKPOINT").read_text(encoding="utf-8").strip()
if re.fullmatch(r"[1-9][0-9]*", checkpoint) is None:
    failures.append(f"CHECKPOINT: expected a positive integer identity, got {checkpoint!r}")
if 'cp "$SELF_DIR/CHECKPOINT" "$REPO/CHECKPOINT"' not in apply:
    failures.append("apply-checkpoint.sh: must propagate the checkpoint identity source")
for name in ("WINDOWS11_COMPAT.md", "WINDOWS11_PREFLIGHT.md", "WINDOWS_UX_CONTRACT.md"):
    first = (root / name).read_text(encoding="utf-8").splitlines()[0]
    if re.search(r"checkpoint\s+[0-9]+", first, re.IGNORECASE):
        failures.append(f"{name}: embeds a checkpoint number instead of using CHECKPOINT as identity")

if failures:
    print("\n".join(f"FAIL {x}" for x in failures), file=sys.stderr)
    raise SystemExit(1)

print("Windows tooling static contract: PASS")
