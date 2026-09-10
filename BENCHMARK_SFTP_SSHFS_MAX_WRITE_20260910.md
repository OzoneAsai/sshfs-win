# SFTP / SSHFS RAMDISK benchmark (2026-09-10)

## Scope

The same mrpio3 `/dev/shm` RAMDISK was accessed from Windows through:

- direct OpenSSH `sftp` (`put` / `get`)
- SSHFS-Win with the default `max_write=32768`
- SSHFS-Win with temporary Launcher option `-o max_write=65536`

Each trial transferred 8 MiB using a 1 MiB client buffer. Writes were flushed
before timing the read. Every successful read returned exactly 8,388,608 bytes.

The `max_write=65536` Launcher registry value was applied only for its test and
restored to `svc %1 %2 %U` afterwards.

## Results

| Transport | Write trials (MiB/s) | Read trials (MiB/s) | Average write | Average read |
|---|---:|---:|---:|---:|
| Direct SFTP | 1.09 / 1.04 / 1.02 | 1.35 / 1.86 / 0.33 | 1.05 | 1.18 |
| SSHFS, `max_write=32768` | 0.49 / 0.50 / 0.47 | 1.99 / 1.89 / 1.93 | 0.49 | 1.94 |
| SSHFS, `max_write=65536` | 1.02 / 0.95 / 1.10 | 2.31 / 2.55 / 2.75 | 1.02 | 2.54 |

## Interpretation

Increasing `max_write` from 32 KiB to 64 KiB approximately doubled SSHFS write
throughput in this run (0.49 to 1.02 MiB/s). Read throughput also increased,
although read values are affected by client/server caching.

The direct SFTP comparison shows that SSHFS adds measurable write overhead even
when the remote target is RAM. The direct SFTP read result has high variance,
so it should not be treated as a stable upper bound.

The earlier benchmark ran while a Next.js build was active on mrpio3; this run
was performed after `campus-calendar.service` had been stopped. Network and
server scheduling variability remain possible.

## Cleanup

Temporary drives, remote RAM test files, and temporary benchmark settings were
removed after the measurement.
