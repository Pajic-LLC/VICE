#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::string read_text(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void test_shader_scene_index_contract() {
    const auto shader = read_text(VICE_RAY_REFLECTIONS_SHADER_SOURCE);
    require(!shader.empty(), "ray-reflection shader source must be readable");
    require(
        shader.find("StructuredBuffer<uint> SceneInstanceIndices : register(t4, space0);") !=
            std::string::npos,
        "ray-reflection shader must consume the authoritative scene-index table through t4"
    );
    require(
        shader.find("CommittedInstanceID()") != std::string::npos &&
            shader.find("CommittedInstanceIndex()") != std::string::npos,
        "reflection tracing must keep persistent identity separate from TLAS array addressing"
    );
    require(
        shader.find("SceneInstanceIndices.GetDimensions") != std::string::npos &&
            shader.find("tlas_instance_index >= scene_index_count") != std::string::npos,
        "reflection scene-index lookup must bounds-check the bound mapping view"
    );
    require(
        shader.find("SceneInstanceIndices[tlas_instance_index]") != std::string::npos &&
            shader.find("scene_instance_index == VICE_INVALID_SCENE_INSTANCE") != std::string::npos,
        "reflection hits must resolve through the authoritative map and reject invalid entries"
    );
    require(
        shader.find("hit.instance_id_plus_one = scene_instance_index + 1u") != std::string::npos,
        "committed reflection hits must publish the authoritative Runtime scene index"
    );
}

void test_runtime_scene_index_binding_contract() {
    const auto implementation = read_text(VICE_RAY_REFLECTIONS_CPP_SOURCE);
    require(!implementation.empty(), "ray-reflection Runtime source must be readable");
    require(
        implementation.find("active_scene_instance_indices()") != std::string::npos &&
            implementation.find("active_scene_instance_index_buffer()") != std::string::npos,
        "Runtime reflections must source both CPU count and GPU mapping from the active TLAS executor"
    );
    require(
        implementation.find("RuntimeRayTracingSceneIndexTable") == std::string::npos &&
            implementation.find("scene_index_table->upload") == std::string::npos,
        "Runtime reflections must not maintain or upload a duplicate TLAS scene-index table"
    );
    require(
        implementation.find("*scene_index_buffer") != std::string::npos &&
            implementation.find("static_cast<std::uint32_t>(active_scene_indices.size())") !=
                std::string::npos,
        "Runtime reflections must dispatch with the executor-owned GPU map and its committed count"
    );
    require(
        implementation.find("reflection_scene_indices_binding") != std::string::npos &&
            implementation.find(".binding = 4U") != std::string::npos &&
            implementation.find("commands.set_compute_buffer(") != std::string::npos,
        "Runtime reflections must bind the scene-index map through the portable compute RHI"
    );
    require(
        implementation.find("active_scene_indices.empty()") != std::string::npos &&
            implementation.find("scene_index_buffer == nullptr") != std::string::npos &&
            implementation.find("committed active TLAS GPU scene-index mapping") !=
                std::string::npos,
        "Runtime reflections must fail closed when the committed CPU/GPU mapping pair is unavailable"
    );
}

} // namespace

int main() {
    test_shader_scene_index_contract();
    test_runtime_scene_index_binding_contract();
    if (failures != 0) {
        std::cerr << failures << " Runtime reflection scene-index contract test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime reflection scene-index contract tests passed\n";
    return 0;
}