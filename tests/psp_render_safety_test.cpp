#include "../platform/psp/psp_render_safety.h"
#include <cassert>
#include <cstdint>

int main() {
    using namespace redch3psp::psp;

    assert(pack_abgr8888(255, 0, 0, 255) == 0xFF0000FFu);
    assert(pack_abgr8888(0, 255, 0, 255) == 0xFF00FF00u);
    assert(pack_abgr8888(0, 0, 255, 255) == 0xFFFF0000u);

    std::uint8_t vertices[3 * 24]{};
    assert(valid_vertex_range({vertices, 3, 24, sizeof(vertices)}, 24));
    assert(!valid_vertex_range({vertices, 4, 24, sizeof(vertices)}, 24));
    assert(!valid_vertex_range({vertices, 3, 12, sizeof(vertices)}, 24));

    assert(valid_index_u16(2, 3));
    assert(!valid_index_u16(3, 3));
    assert(finite3(0.0f, 1.0f, -1.0f));

    return 0;
}
