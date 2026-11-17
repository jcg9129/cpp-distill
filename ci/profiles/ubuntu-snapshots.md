<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Recorded Ubuntu snapshots

Both releases use **20261117T000000Z**. On 2026-11-17 we downloaded only the
following metadata (no compiler binaries), decompressed it, and checked the
Package, Version, Filename and SHA256 fields for all nine packages enumerated
by `ubuntu.packages()` / `install-ubuntu.sh`:

| Release | Snapshot index | SHA256 of Packages.xz | All nine revisions |
| --- | --- | --- | --- |
| 24.04 | [noble-updates/main/amd64](https://snapshot.ubuntu.com/ubuntu/20261117T000000Z/dists/noble-updates/main/binary-amd64/Packages.xz) | `04b862616ab280fb804b7e4b76644267c20c8c2539f4f4a667294f56a4259762` | `13.3.0-6ubuntu2~24.04.1` |
| 26.04 | [resolute/main/amd64](https://snapshot.ubuntu.com/ubuntu/20261117T000000Z/dists/resolute/main/binary-amd64/Packages.xz) | `ed9ac41cb263efaecc5a06a0742dc56570e2fb5ff94c4f2e4b2fbdcddd73cf24` | `15.2.0-16ubuntu1` |

The corresponding Release files date from 2026-11-16 17:31:46 UTC and
2026-04-23 17:07:15 UTC. The nine records include both the small frontend
packages and their x86-64 binaries, base and development packages; finding
only `g++` would not suffice. Paths are under `pool/main/g/gcc-13/` and
`pool/main/g/gcc-15/` respectively. This is direct evidence that the pinned
revisions exist at the chosen timestamps, rather than an inference from the
current archive. [Ubuntu documents timestamped acquisition](https://snapshot.ubuntu.com/).

APT uses only the recorded snapshot, retains signature verification, and
checks every installed revision. Availability hashes the compressed indexes
against Release metadata, then downloads every exact `.deb` and checks its
SHA256 and size from that index. This metadata check uses HTTPS; the installer
additionally authenticates the Release with Ubuntu's archive keyring. Runtime
packages remain the runner's versions as before; this is still an approximate
snapshot. CI reproduction must confirm unchanged output bytes.
