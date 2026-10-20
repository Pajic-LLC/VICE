#include <vice/tools/shader_cooker.hpp>

#include <cassert>
#include <string_view>

int main() {
    using namespace vice::tools::shader_cooker;

    constexpr std::string_view standard = R"DXC(
; Pipeline Runtime Information:
;
; NumThreads=(8,4,2)
;
; Resource Bindings:
)DXC";
    const auto reflected = parse_dxc_compute_thread_group_size(standard);
    assert(reflected.has_value());
    assert(reflected->x == 8U);
    assert(reflected->y == 4U);
    assert(reflected->z == 2U);

    constexpr std::string_view spaced = "; NumThreads = ( 16, 2, 1 )\n";
    const auto spaced_reflection = parse_dxc_compute_thread_group_size(spaced);
    assert(spaced_reflection.has_value());
    assert(spaced_reflection->x == 16U);
    assert(spaced_reflection->y == 2U);
    assert(spaced_reflection->z == 1U);

    assert(!parse_dxc_compute_thread_group_size("; no compute metadata\n").has_value());
    assert(!parse_dxc_compute_thread_group_size("; NumThreads=(0,1,1)\n").has_value());
    assert(!parse_dxc_compute_thread_group_size("; NumThreads=(8,1)\n").has_value());
    assert(!parse_dxc_compute_thread_group_size("; NumThreads=(8,1,1,1)\n").has_value());
    assert(!parse_dxc_compute_thread_group_size("; NumThreads=(8,nope,1)\n").has_value());

    return 0;
}
