#!/usr/bin/env python3
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "sshfs-win.c").read_text(encoding="utf-8")

start = source.index("static void normalize_launcher_argv")
end = source.index("#else", start)
body = source[start:end]

for token in (
    "cygwin_argc = *argc;",
    "status=active count-mismatch",
    "authority=native",
    "*argc = native_argc;",
    "*argv = native_argv;",
):
    if token not in body:
        raise SystemExit(f"normalize_launcher_argv missing native-authority contract: {token}")

for forbidden in (
    "count-mismatch native=%d cygwin=%d fallback=cygwin",
    "win32_free_parsed_argv(native_argv);\n        return;",
):
    if forbidden in body:
        raise SystemExit(f"normalize_launcher_argv still discards recovered native argv: {forbidden}")

print("Native launcher argv authority contract: PASS")
