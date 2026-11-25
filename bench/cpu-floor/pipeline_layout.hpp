// Proposta de ABI; não é um exportador implementado no decoder.
#pragma once
#include <cstddef>
#include <cstdint>
namespace f30 {
struct Macroblock {
    uint32_t type, flags, coefficientOffset;
    uint16_t sliceId, availableNeighbors;
    uint8_t qpY, qpCb, qpCr, disableDeblock;
    int8_t alphaOffset, betaOffset;
    uint8_t list0Count, list1Count;
    uint16_t weightsIndex, scalingIndex;
    uint8_t intra16Mode, intraChromaMode, transformSize, reserved0;
    uint8_t intra4Mode[16], nonzero[24], boundaryStrength[2][4][4];
    int8_t reference[2][16];
    int16_t motion[2][16][2];
    uint8_t reserved1[56];
};
struct Coefficients16 { int16_t sample[384], lumaDC[16]; };
struct Coefficients32 { int32_t sample[384], lumaDC[16]; };
static_assert(sizeof(Macroblock) == 320);
static_assert(offsetof(Macroblock, motion) == 136);
static_assert(sizeof(Coefficients16) == 800);
static_assert(sizeof(Coefficients32) == 1600);
constexpr size_t MB_COUNT = 120 * 68;
constexpr size_t HEADER_BYTES = 64 * 1024;
constexpr size_t FRAME16_BYTES = HEADER_BYTES + MB_COUNT * (sizeof(Macroblock) + sizeof(Coefficients16));
constexpr size_t FRAME32_BYTES = HEADER_BYTES + MB_COUNT * (sizeof(Macroblock) + sizeof(Coefficients32));
static_assert(FRAME16_BYTES == 9204736);
static_assert(FRAME32_BYTES == 15732736);
// O wire format deve ser escrito explicitamente em little endian, nunca memcpy do struct Xenon.
}
