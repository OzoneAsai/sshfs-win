# Dependency policy — checkpoint 8

This checkpoint stops treating the Cygwin build machine as an implicit dependency lockfile.

## Runtime policy

- WinFsp baseline: 2.1.25156 or newer in the 2.1 line.
- Cygwin baseline: 3.6.10 stable. 3.7 development snapshots are not selected by default.
- GLib baseline: 2.88.3 in the current 2.88.x stable line. Development 2.89.x is intentionally not chased merely because it is newer.
- SSHFS core remains pinned to `24448e2493533ead984d6ca322c583e1a26cc613` until a newer upstream commit is separately audited.
- Legacy OpenSSL 1.0/1.1 runtime DLLs are forbidden.
- OpenSSL 3.0 is not accepted for the vendor SSH path: its upstream support ends 2026-09-07.

## SSH/crypto policy

The historical Makefile copied `/usr/bin/ssh.exe` plus whatever DLLs happened to be installed on the build host. This is no longer the preferred path.

`VendorSSH=1` builds an explicitly locked client stack from source:

- OpenSSL 3.5.8 LTS, commit `f4dc4d58b48d346a8270183f89acf826d459b0ca`.
- OpenSSH portable 10.5p1, signed-tag commit `b3f7344209832eea8ece447d871ea748767c444b`.

OpenSSL 3.5 was selected instead of 4.0 because 3.5 is the long-term support branch through 2030 while 4.0 is a short-lived feature branch. The objective is a strong maintained cryptographic floor, not the largest version number.

The source-built SSH path is deliberately separate from Windows native OpenSSH. Native OpenSSH remains useful for compatibility experiments, but a Windows installation may carry an older inbox version and therefore cannot be treated as a reproducible security baseline.

## Reproducibility

`tools/check-dependencies.sh` checks build dependencies before packaging.
`tools/build-vendor-ssh.sh` checks out exact Git commits and refuses SHA drift.
`tools/write-dependency-manifest.sh` records packaged files, hashes, and owning Cygwin packages.
`tools/audit-runtime.sh` rejects known legacy crypto DLL names.

The bundled pristine SSHFS source is verified without nested repository metadata:
`tools/verify-sshfs-source.sh` reconstructs the Git tree and requires
`35655f60d37403663238a73b4204cfd64fdca73c`, corresponding to the pinned
`24448e2493533ead984d6ca322c583e1a26cc613` source snapshot. File modes are
part of this identity, so source-archive mode loss is detected before patching.

No dependency is considered upgraded merely because `cygcheck` happened to find a newer DLL on one developer machine.
