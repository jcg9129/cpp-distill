# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared validation of judge and snapshot compiler identities."""
import re


def identity(profile, key):
    if key not in profile:
        return 'none'
    value = profile[key]
    if not isinstance(value, dict) or len(value) != 1 or next(iter(value)) not in ('package', 'recipe'):
        raise ValueError(f'{key} requires exactly package or recipe')
    kind, text = next(iter(value.items()))
    if (not isinstance(text, str) or not text or any(ord(c) < 32 or ord(c) == 127 for c in text)
            or (kind == 'recipe' and not re.fullmatch(r'https?://[^ \t]+', text))
            or (kind == 'package' and not re.fullmatch(r'[A-Za-z0-9_.:+-]+=[A-Za-z0-9_.:+~-]+', text))):
        raise ValueError(f'{key}: invalid compiler provenance (package requires an exact version)')
    return kind + '=' + text
