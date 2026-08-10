/*
 * CRC-8 SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF) — the checksum used on
 * every periodic DrowsyGuard CAN frame (specs/04-interface-control-document.md
 * CAN-011). Both nodes on the bus must compute byte-identical output; that is
 * why this file carries its own self-test vectors rather than trusting one
 * implementation to be right (see specs/04 OI-04-04 / CAN-070). The vectors
 * in crc8.c are copied from ../../../shared/icd/crc_vectors.csv — the
 * canonical copy — since firmware can't read a CSV off flash at runtime;
 * keep both copies identical by hand (see ../../../shared/icd/README.md).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DG_CRC8_H_
#define DG_CRC8_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t DG_Crc8(const uint8_t *data, size_t len);

/* Runs the fixed test vectors in crc8.c against DG_Crc8() and returns true
 * only if every one matches. Call once at boot and refuse to arm on failure
 * — a CRC mismatch between nodes discards every frame silently (CAN-011),
 * and that failure mode must never be discovered on the demo bench. */
bool DG_Crc8SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* DG_CRC8_H_ */
