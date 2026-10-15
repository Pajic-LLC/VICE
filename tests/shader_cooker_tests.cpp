#include <vice/tools/shader_cooker.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    using namespace vice::tools::shader_cooker;

    assert(shader_stage_name(ShaderStage::vertex) == "vertex");
    assert(shader_stage_name(ShaderStage::fragment) == "fragment");
    assert(shader_stage_name(ShaderStage::compute) == "compute");

    assert(shader_profile(ShaderStage::vertex, "6.0") == "vs_6_0");
    assert(shader_profile(ShaderStage::fragment, "6.6") == "ps_6_6");
    assert(shader_profile(ShaderStage::compute, "6.9") == "cs_6_9");

    CookRequest request;
    request.entry_point = "main_vs";
    request.stage = ShaderStage::vertex;
    request.target = CookTarget::all;
    request.shader_model = "6.0";

    const std::string source =
        "float4 main_vs(uint vertex_id : SV_VertexID) : SV_Position { "
        "return float4(vertex_id, 0, 0, 1); }";

    const auto first = make_cache_key(source, request, "dxc 1", "msc 1");
    const auto second = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first == second);
    assert(first.size() == 64U);

    request.entry_point = "other_vs";
    const auto changed_entry = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first != changed_entry);

    request.entry_point = "main_vs";
    request.debug = true;
    const auto debug_key = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first != debug_key);

    const auto temporary =
        std::filesystem::temp_directory_path() / "vice_shader_cooker_discovery_test";
#ifdef _WIN32
    const auto fake_tool = temporary / "vice-fake-tool.exe";
#else
    const auto fake_tool = temporary / "vice-fake-tool";
#endif
    std::filesystem::create_directories(temporary);
    {
        std::ofstream stream(fake_tool);
        stream << "fake";
    }
    assert(discover_executable(fake_tool, "VICE_TEST_UNUSED", "vice-fake-tool") == fake_tool);
    std::filesystem::remove_all(temporary);

    return 0;
}
