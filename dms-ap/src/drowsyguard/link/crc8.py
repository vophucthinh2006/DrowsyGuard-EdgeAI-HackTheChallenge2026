"""CRC-8 SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF).

Mirrors drowsyguard_vcs/src/icd/crc8.c byte-for-byte, including the exact
same test vectors (specs/04-interface-control-document.md CAN-070 wants both
nodes' CRC implementations verified against a shared vector set before
integration -- until the shared/icd/crc_vectors.csv file from specs 02
DEV-002 exists, `TEST_VECTORS` below and the C array in crc8.c ARE that
shared set, kept in sync by hand. If you change one, change the other.).
"""

from __future__ import annotations

_POLY = 0x1D
_INIT = 0xFF
_XOROUT = 0xFF


def crc8(data: bytes) -> int:
    crc = _INIT
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ _POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc ^ _XOROUT


# Identical to the kVecA..kVecF vectors in drowsyguard_vcs/src/icd/crc8.c.
TEST_VECTORS: list[tuple[bytes, int]] = [
    (b"", 0x00),
    (bytes([0x00]), 0x3B),
    (bytes([0xFF]), 0xFF),
    (b"123456789", 0x4B),
    (bytes([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06]), 0x0B),
    (bytes([0xA5, 0x5A, 0x00, 0xFF, 0x01, 0xFE, 0x81]), 0x2D),
    (bytes([0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE]), 0xB1),
]


def self_test() -> bool:
    return all(crc8(data) == expected for data, expected in TEST_VECTORS)
