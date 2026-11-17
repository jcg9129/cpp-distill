# SPDX-License-Identifier: GPL-3.0-or-later
"""Strict subset of RFC 4880: v4 binary-document RSA signatures, SHA256/512.

No trust discovery: exact public key packet fingerprints are pinned in the
reviewed keyring. Unsupported algorithms/packets fail closed. Nothing executes
GPG, package scripts or archive members. RSA PKCS#1 v1.5 checks the entire
DigestInfo and padding, not merely OpenPGP's two-byte hash hint.
"""
import hashlib
from common import IntegrityError


def packets(data):
    offset = 0
    while offset < len(data):
        tag = data[offset]
        offset += 1
        if not tag & 128:
            raise ValueError('invalid OpenPGP packet')
        if tag & 64:
            tag &= 63
            first = data[offset]
            offset += 1
            if first < 192:
                size = first
            elif first < 224:
                size = (first - 192) * 256 + data[offset] + 192
                offset += 1
            elif first == 255:
                size = int.from_bytes(data[offset:offset + 4], 'big')
                offset += 4
            else:
                raise ValueError('partial OpenPGP packet unsupported')
        else:
            length_type = tag & 3
            tag = (tag >> 2) & 15
            if length_type == 3:
                raise ValueError('indeterminate OpenPGP packet unsupported')
            width = 1 << length_type
            size = int.from_bytes(data[offset:offset + width], 'big')
            offset += width
        body = data[offset:offset + size]
        if len(body) != size:
            raise ValueError('truncated OpenPGP packet')
        offset += size
        yield tag, body


def mpi(data, offset):
    bits = int.from_bytes(data[offset:offset + 2], 'big')
    end = offset + 2 + (bits + 7) // 8
    value = int.from_bytes(data[offset + 2:end], 'big')
    if end > len(data) or value.bit_length() != bits:
        raise ValueError('invalid OpenPGP integer')
    return value, end


def keys(keyring):
    result = {}
    for tag, body in packets(keyring):
        if tag not in (6, 14) or body[0] != 4 or body[5] not in (1, 3):
            continue
        fingerprint = hashlib.sha1(b'\x99' + len(body).to_bytes(2, 'big') + body).hexdigest()
        modulus, offset = mpi(body, 6)
        exponent, offset = mpi(body, offset)
        if offset != len(body) or modulus.bit_length() < 2048:
            raise ValueError('invalid pinned RSA key')
        result[fingerprint] = (modulus, exponent)
    return result


def signed_digest(signature, public_keys):
    """Return (expected digest, suffix) for streaming the signed package bytes."""
    parsed = list(packets(signature))
    if len(parsed) != 1 or parsed[0][0] != 2:
        raise ValueError('expected one detached signature')
    body = parsed[0][1]
    if body[:3] != b'\x04\x00\x01' or body[3] not in (8, 10):
        raise ValueError('unsupported detached signature algorithm/type')
    hashed_end = 6 + int.from_bytes(body[4:6], 'big')
    hashed = body[:hashed_end]
    # Match a pinned issuer fingerprint in the hashed subpackets, never by a
    # short issuer ID or an unauthenticated fetched key.
    fingerprints = []
    offset = 6
    while offset < hashed_end:
        size = body[offset]
        if not 1 <= size < 192 or offset + 1 + size > hashed_end:
            raise ValueError('unsupported signature subpacket')
        subpacket = body[offset + 1:offset + 1 + size]
        if subpacket[:2] == b'\x21\x04' and len(subpacket) == 22:
            fingerprints.append(subpacket[2:].hex())
        offset += 1 + size
    if len(fingerprints) != 1 or fingerprints[0] not in public_keys:
        raise IntegrityError('signature has no unique pinned signing fingerprint')
    offset = hashed_end + 2 + int.from_bytes(body[hashed_end:hashed_end + 2], 'big')
    hint = body[offset:offset + 2]
    signature_number, end = mpi(body, offset + 2)
    modulus, exponent = public_keys[fingerprints[0]]
    if end != len(body) or signature_number >= modulus:
        raise IntegrityError('invalid RSA signature')
    width = (modulus.bit_length() + 7) // 8
    encoded = pow(signature_number, exponent, modulus).to_bytes(width, 'big')
    algorithm, prefix, length = ('sha256', '3031300d060960864801650304020105000420', 32) if body[3] == 8 else ('sha512', '3051300d060960864801650304020305000440', 64)
    digest_info = bytes.fromhex(prefix) + encoded[-length:]
    padding = width - len(digest_info) - 3
    if padding < 8 or encoded != b'\x00\x01' + b'\xff' * padding + b'\x00' + digest_info or hint != encoded[-length:][:2]:
        raise IntegrityError('invalid RSA PKCS#1 signature padding/hash')
    suffix = hashed + b'\x04\xff' + len(hashed).to_bytes(4, 'big')
    return algorithm + ':' + encoded[-length:].hex(), suffix
