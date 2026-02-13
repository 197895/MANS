#pragma once

#include <cstdint>

namespace mans {

struct MansParams {
    std::uint32_t backend;       // 0: CPU, 1: GPU
    std::uint32_t dtype;         // 0: U16, 1: U32
    std::uint32_t adm_threshold; // (block max diff > adm_threshold) -> skip adm mode
    std::uint32_t adm_decide_threads;

    std::uint32_t adm_center_calc_threads;
    std::uint32_t adm_encode_threads;
    std::uint32_t adm_warp_reduce_threads;
    std::uint32_t adm_fill_tail_threads;
    std::uint32_t adm_write_back_threads;

    std::uint32_t adm_restore_signals_threads;
    std::uint32_t adm_decode_values_threads;
};

static_assert(sizeof(MansParams) % 4 == 0, "MansParams size must be multiple of 4 bytes");

namespace Backend {
constexpr std::uint32_t CPU = 0;
constexpr std::uint32_t NVIDIA = 1;
} // namespace Backend

namespace DataType {
constexpr std::uint32_t U16 = 0;
constexpr std::uint32_t U32 = 1;
} // namespace DataType

struct MansHeader {
    std::uint8_t codec; // 1 = ADM, 2 = ANS
};
static_assert(sizeof(MansHeader) == 1, "MansHeader must be 1 byte");

} // namespace mans
