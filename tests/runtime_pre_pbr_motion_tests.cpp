#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {
[[nodiscard]] std::string read_text_file(const char* const path) {
    std::ifstream input{path, std::ios::binary};
    if (!input.good()) return {};
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
}
} // namespace

int main() {
#ifndef VICE_RUNTIME_SCENE_LEGACY_SOURCE
    return 0;
#else
    const std::string source = read_text_file(VICE_RUNTIME_SCENE_LEGACY_SOURCE);
    CHECK(!source.empty());

    const auto encoder = source.find("const auto encode_motion_vectors = [&]() -> bool");
    const auto motion_pass = source.find("begin_render_pass(*state_->motion_vectors, motion_pass)", encoder);
    const auto depth_done = source.find("VICE Runtime could not finish the camera depth prepass.");
    const auto early_motion = source.find("if (!encode_motion_vectors()) return false;", depth_done);
    const auto hdr_begin = source.find("VICE Runtime could not begin the HDR scene render pass.");
    const auto hdr_done = source.find("VICE Runtime could not finish the HDR scene render pass.");
    const auto fallback_motion = source.find(
        "if (!motion_vectors_encoded && !encode_motion_vectors())", hdr_done
    );
    const auto taa_motion = source.find(".motion_vectors = state_->motion_vectors.get()");

    CHECK(encoder != std::string::npos);
    CHECK(motion_pass != std::string::npos);
    CHECK(depth_done != std::string::npos);
    CHECK(early_motion != std::string::npos);
    CHECK(hdr_begin != std::string::npos);
    CHECK(hdr_done != std::string::npos);
    CHECK(fallback_motion != std::string::npos);
    CHECK(taa_motion != std::string::npos);

    CHECK(encoder < depth_done);
    CHECK(depth_done < early_motion);
    CHECK(early_motion < hdr_begin);
    CHECK(hdr_begin < hdr_done);
    CHECK(hdr_done < fallback_motion);
    CHECK(fallback_motion < taa_motion);

    CHECK(source.find(
        "begin_render_pass(*state_->motion_vectors, motion_pass)", motion_pass + 1U
    ) == std::string::npos);
    CHECK(source.find("motion_pass.depth_attachment = current_depth") != std::string::npos);
    CHECK(source.find("motion_pass.clear_depth_attachment = false") != std::string::npos);

    return 0;
#endif
}

#undef CHECK
