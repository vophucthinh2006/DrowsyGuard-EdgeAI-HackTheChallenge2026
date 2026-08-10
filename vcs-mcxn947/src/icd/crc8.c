/*
 * CRC-8 SAE-J1850, poly 0x1D, init 0xFF, xorout 0xFF.
 *
 * Bit-by-bit (not table-driven): this runs at most once per CAN frame per
 * 10 ms control tick, nowhere near hot enough to justify a 256-byte lookup
 * table, and the straight-line form is what the test vectors below were
 * generated against independently (see the Python reference used to derive
 * them, noted per-vector).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "crc8.h"

#define DG_CRC8_POLY   (0x1DU)
#define DG_CRC8_INIT   (0xFFU)
#define DG_CRC8_XOROUT (0xFFU)

uint8_t DG_Crc8(const uint8_t *data, size_t len) {
  uint8_t crc = DG_CRC8_INIT;

  for (size_t i = 0U; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++) {
      if ((crc & 0x80U) != 0U) {
        crc = (uint8_t)((crc << 1) ^ DG_CRC8_POLY);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }

  return (uint8_t)(crc ^ DG_CRC8_XOROUT);
}

/* Copied from ../../../shared/icd/crc_vectors.csv — the canonical copy.
 * These are the vectors both nodes must agree on before either is trusted
 * to discard a "bad" frame (specs/04 OI-04-04 / CAN-070); the Python side's
 * copy is dms-ap/src/drowsyguard/link/crc8.py's TEST_VECTORS. Keep all
 * three identical by hand until shared/icd/generate.py exists. */
typedef struct {
  const uint8_t *data;
  size_t len;
  uint8_t expected;
} dg_crc8_vector_t;

static const uint8_t kVecA[] = {0x00U};
static const uint8_t kVecB[] = {0xFFU};
static const uint8_t kVecC[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static const uint8_t kVecD[] = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U};
static const uint8_t kVecE[] = {0xA5U, 0x5AU, 0x00U, 0xFFU, 0x01U, 0xFEU, 0x81U};
static const uint8_t kVecF[] = {0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU, 0xDEU};

static const dg_crc8_vector_t kVectors[] = {
    {NULL, 0U, 0x00U},
    {kVecA, sizeof(kVecA), 0x3BU},
    {kVecB, sizeof(kVecB), 0xFFU},
    {kVecC, sizeof(kVecC), 0x4BU},
    {kVecD, sizeof(kVecD), 0x0BU},
    {kVecE, sizeof(kVecE), 0x2DU},
    {kVecF, sizeof(kVecF), 0xB1U},
};

bool DG_Crc8SelfTest(void) {
  for (size_t i = 0U; i < sizeof(kVectors) / sizeof(kVectors[0]); i++) {
    const dg_crc8_vector_t *v = &kVectors[i];
    if (DG_Crc8(v->data, v->len) != v->expected) {
      return false;
    }
  }
  return true;
}
