from drowsyguard.link.crc8 import self_test


def test_crc8_self_test_vectors_match_c_implementation():
    """CAN-070: this Python CRC-8 and drowsyguard_vcs/src/icd/crc8.c's must
    agree on every vector. If this fails, DO NOT touch the vectors to make
    it pass -- one of the two implementations is wrong."""
    assert self_test()
