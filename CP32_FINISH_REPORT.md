# CP32-A finishing report

CP32-A begins the finishing phase after CP31-Z. It does not add new filesystem
features. It closes source-artifact and reproducibility defects that prevented a
checkpoint archive from being a self-contained build input.

## Source archive self-containment

The checkpoint archives intentionally do not carry nested Git object stores,
but the previous Makefile still ran `git clone $(PrjDir)/sshfs`, queried
`rev-parse` in the staged tree, and used `git clean` for cleanup. An extracted
checkpoint therefore depended on repository metadata that was not present in
the artifact.

The pinned pristine SSHFS source now stages via a normal directory copy. Before
patching, `tools/verify-sshfs-source.sh` reconstructs the Git tree from source
bytes and file modes and requires:

- commit provenance: `24448e2493533ead984d6ca322c583e1a26cc613`
- source tree: `35655f60d37403663238a73b4204cfd64fdca73c`
- `sshfs.c` blob: `3bd4ec57b3bf7c5eb8aff68dafa6a84264186269`

The executable bit on upstream `sshfs/test/test_sshfs.py` is restored; without
that mode the source bytes looked correct but the Git tree identity differed.
Patch application remains explicit through `patches/SERIES`, and `git apply`
is used only as a patch engine rather than as proof that the source directory is
a repository.

`make clean` now removes `.build` directly and works from a source archive.
New staging and cleanup paths are quoted for Windows/Cygwin paths containing
spaces.

## Artifact hygiene

Generated `.cp31g-test2` binaries/logs inherited from older working checkpoints
are removed. `tests/artifact-hygiene-test.sh` rejects build/index/cache/log/object
residue in future source artifacts.

## New contracts

- `tests/source-archive-self-contained-test.sh`
- `tests/artifact-hygiene-test.sh`
- expanded `tests/windows-tooling-static-test.py`

## Release boundary

CP32-A is a source/reproducibility finishing checkpoint. The remaining release
gates are native Cygwin compilation/packaging and Windows 11 24H2/25H2 runtime
mount tests; this Linux validation does not claim those have run.

## CP32-A validation

- pinned pristine SSHFS source identity PASS: tree `35655f60d37403663238a73b4204cfd64fdca73c`
- 47/47 patches cleanly apply in SERIES order
- patched `sshfs.c` is byte-identical to CP31-Z effective core
- CodeGraph effective tree: 16 files / 443 nodes / 1,282 edges
- 45/45 strict C tests PASS
- 45/45 ASan+UBSan C tests PASS
- Windows tooling static contract PASS
- source archive self-contained contract PASS
- artifact hygiene contract PASS
- patch-series contract PASS
- distribution signing contract PASS (including expected unsigned-test warning path)
- all shell scripts pass `bash -n`
- wrapper strict compile PASS
- BOM scan: zero files
