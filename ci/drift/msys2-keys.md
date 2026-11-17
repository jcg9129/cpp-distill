<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# MSYS2 package digest verification

The exact filenames remain in `ci/profiles/mingw-packages.txt`. Historical
packages have detached `.sig` files rather than SHA256 sidecars. Availability
streams the complete package into the OpenPGP binary-document hash, including
the v4 signature suffix, and compares the full SHA512 digest with the RSA
PKCS#1 v1.5 signed DigestInfo. Altered bytes, padding, hash or unknown signing
fingerprints are integrity alerts. Unsupported algorithms fail closed.

`msys2-keys.json` contains only the public RSA modulus and exponent for the
signing fingerprints used by those 16 packages. All 16 small detached signatures
were fetched and checked for supported format on 2026-11-17; no package binaries
were downloaded locally. The keys came from [MSYS2's official public keyring](https://github.com/msys2/MSYS2-keyring/blob/5d249077a0cfeec9dbf51515e5e01da413d28859/msys2.gpg),
whose original ASCII-armored bytes have SHA256
`1257d4ccc536c53333445a618039af4c3ea3f56b96601de9af89151ea529c19a`.
The v4 key packet SHA1 fingerprints (OpenPGP identifiers, not artifact digests)
are pinned as JSON keys. Key changes require human review; no keyserver lookup
or implicit trust extension occurs in CI. This intentionally implements only
the needed RFC 4880 RSA/SHA256/SHA512 subset in stdlib Python; synthetic signed
fixtures exercise full digest verification and malformed/tampered signatures.
