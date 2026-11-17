# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared validation of judge and snapshot compiler identities."""
import re


# Ubuntu identities include the release and a Debian revision, with optional epoch.
UBUNTU_PACKAGE = r'ubuntu-[0-9]{2}\.[0-9]{2}:[a-z0-9][a-z0-9+.-]*=(?:[0-9]+:)?[0-9][A-Za-z0-9.+~-]*-[A-Za-z0-9.+~]+'
# Snapshot-only OCI identity: explicit Docker Hub registry/repository, no tag,
# lowercase SHA-256 manifest/index digest. Keep in sync with the C++ schema.
IMAGE = r'docker\.io/[a-z0-9]+(?:[._-][a-z0-9]+)*(?:/[a-z0-9]+(?:[._-][a-z0-9]+)*)+@sha256:[0-9a-f]{64}'
# Snapshot-only Homebrew core GCC bottle blob, not a judge build identity.
HOMEBREW = r'homebrew:gcc=[1-9][0-9]*\.[0-9]+\.[0-9]+@sha256:[0-9a-f]{64}'
# Official WinLibs release tag/asset plus archive blob digest, snapshot only.
WINLIBS = r'winlibs:[A-Za-z0-9][A-Za-z0-9._+-]*/[A-Za-z0-9][A-Za-z0-9._+-]*\.(?:zip|7z)@sha256:[0-9a-f]{64}'


def identity(profile, key):
    if key not in profile:
        return 'none'
    value = profile[key]
    kinds = ('package', 'recipe', 'image', 'homebrew', 'winlibs') if key == 'snapshot_compiler' else ('package', 'recipe')
    if not isinstance(value, dict) or len(value) != 1 or next(iter(value)) not in kinds:
        raise ValueError(f'{key} requires exactly one of {", ".join(kinds)}')
    kind, text = next(iter(value.items()))
    if (not isinstance(text, str) or not text or any(ord(c) < 32 or ord(c) == 127 for c in text)
            or (kind == 'recipe' and not re.fullmatch(r'https?://[^ \t]+', text))
            or (kind == 'image' and not re.fullmatch(IMAGE, text))
            or (kind == 'homebrew' and not re.fullmatch(HOMEBREW, text))
            or (kind == 'winlibs' and not re.fullmatch(WINLIBS, text))
            or (kind == 'package' and not re.fullmatch(r'[A-Za-z0-9_.:+-]+=[A-Za-z0-9_.:+~-]+', text))):
        raise ValueError(f'{key}: invalid compiler provenance (package requires an exact version; image/homebrew/winlibs require a digest)')
    if kind == 'package' and text.startswith(('ubuntu-', 'ubuntu:')) and not re.fullmatch(UBUNTU_PACKAGE, text):
        raise ValueError(f'{key}: Ubuntu package requires ubuntu-YY.MM:name=version-revision')
    return kind + '=' + text
