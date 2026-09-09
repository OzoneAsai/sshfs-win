# Unicode Windows argument handling — current checkpoint

The forkless Cygwin launcher ultimately calls `CreateProcessW`. Checkpoint 12
converted SSH arguments with locale-dependent `mbstowcs()` without explicitly
activating the process locale first.

Checkpoint 14 makes the conversion contract explicit:

1. `processing_init()` activates `LC_CTYPE` with `setlocale(LC_CTYPE, "")`;
2. `win32_widen_arg()` first decodes according to that selected Cygwin locale;
3. if locale decoding rejects the byte string, it performs a strict UTF-8
   fallback with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)`;
4. invalid input is rejected rather than silently replacing characters;
5. the existing Windows command-line quoting algorithm remains unchanged.

The UTF-8 fallback is intentional. Current Cygwin documentation states that the
locale used for application multibyte conversion should be activated with
`setlocale()`, that the no-variable default is `C.UTF-8`, and that Cygwin still
uses UTF-8 internally for filename conversion when an ASCII C/POSIX locale is
selected.

The regression corpus contains Japanese, Chinese and Cyrillic arguments plus
quote/backslash edge cases. This directly targets the class of failures reported
by sshfs-win issue #393 for a Chinese remote path. Issue #418 uses the password
launcher path and therefore is not claimed fixed by this forkless-only change.

## Validation boundary

The portable locale/quoting regression test passes on Linux and the CP12→CP14
review fixture applies with `patch --fuzz=0` and byte-matches the expected file.
The Win32 `MultiByteToWideChar()` fallback still requires a Cygwin/Windows build
and runtime test. This checkpoint does not treat fixture application as proof
that the complete historical SSHFS patch stack applies to the full upstream
`sshfs.c`; that consolidation remains a separate task.

## Native command-line parser differential contract

The wrapper parser also follows the Microsoft C runtime rule that a pair of
double quotes inside a quoted span represents one literal double quote. The
regression suite contains a Microsoft-documented corpus case in addition to
self-generated quote/backslash round trips; this prevents a quote generator and
parser from sharing the same mistaken grammar while still passing a property
test.
