#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::string read_text(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main() {
    const auto shader = read_text(VICE_RAY_REFLECTION_HISTORY_SHADER_SOURCE);
    require(!shader.empty(), "reflection-history shader source must be readable");
    require(shader.find("vice_history_depth_matches") != std::string::npos,
        "reconstruction must validate each history tap against depth");
    require(shader.find("for (uint sample_index = 0u; sample_index < 4u; ++sample_index)") != std::string::npos,
        "reconstruction must gather the four bilinear history taps");
    require(shader.find("reconstructed += max(history_radiance, 0.0) * weight") != std::string::npos,
        "validated history taps must contribute with bilinear weights");
    require(shader.find("if (total_weight < 0.5) return") != std::string::npos,
        "insufficient history support must fail closed to current reflection fallback");
    require(shader.find("reconstructed / total_weight") != std::string::npos,
        "surviving history taps must be renormalized after disocclusion rejection");
    require(shader.find("current_radiance.a") != std::string::npos,
        "reconstruction must preserve the current-frame PBR replacement mask");
    if (failures != 0) {
        std::cerr << failures << " reflection reconstruction contract test(s) failed\n";
        return 1;
    }
    return 0;
}
