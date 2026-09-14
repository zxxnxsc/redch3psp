#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

namespace redch3psp { namespace psp {

// sceGu uses ABGR for GU_PSM_8888/GU_COLOR_8888: 0xAABBGGRR.
constexpr std::uint32_t pack_abgr8888(std::uint8_t r, std::uint8_t g,
                                      std::uint8_t b, std::uint8_t a) noexcept {
    return (std::uint32_t(a) << 24) |
           (std::uint32_t(b) << 16) |
           (std::uint32_t(g) << 8)  |
            std::uint32_t(r);
}

static_assert(pack_abgr8888(255, 0, 0, 255) == 0xFF0000FFu, "PSP ABGR red");
static_assert(pack_abgr8888(0, 255, 0, 255) == 0xFF00FF00u, "PSP ABGR green");
static_assert(pack_abgr8888(0, 0, 255, 255) == 0xFFFF0000u, "PSP ABGR blue");

struct VertexRange {
    const void *data;
    std::size_t count;
    std::size_t stride;
    std::size_t buffer_bytes;
};

inline bool valid_vertex_range(const VertexRange &v, std::size_t minimum_stride) noexcept {
    if (v.count == 0) return true;
    if (v.data == nullptr || v.stride < minimum_stride) return false;
    if (v.count > (static_cast<std::size_t>(-1) / v.stride)) return false;
    return v.count * v.stride <= v.buffer_bytes;
}

inline bool finite3(float x, float y, float z) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

inline bool valid_index_u16(std::uint16_t index, std::size_t vertex_count) noexcept {
    return static_cast<std::size_t>(index) < vertex_count;
}

}} // namespace redch3psp::psp
