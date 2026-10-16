#include <vice/tools/mesh_cooker.hpp>

#if VICE_MESH_COOKER_COLLISION
#include <vice/tools/collision_cooker.hpp>
#include <vice/tools/collision_import.hpp>
#endif

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "VICE mesh cooker\n\n"
        << "Usage:\n"
        << "  vice_mesh_cooker --input <source.vmesh> --output <asset.vmeshc> "
           "[--collision-output <asset.vcollisionc>]\n";
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return std::nullopt;
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) return std::nullopt;
    return contents.str();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::filesystem::path collision_output_path;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto require_value = [&](const char* option) -> std::optional<std::string_view> {
            if (index + 1 >= argc) {
                std::cerr << option << " requires a value.\n";
                return std::nullopt;
            }
            ++index;
            return std::string_view{argv[index]};
        };
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--input") {
            const auto value = require_value("--input");
            if (!value) return 2;
            input_path = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--output") {
            const auto value = require_value("--output");
            if (!value) return 2;
            output_path = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--collision-output") {
            const auto value = require_value("--collision-output");
            if (!value) return 2;
            collision_output_path = std::filesystem::path{*value};
            continue;
        }
        std::cerr << "Unknown option: " << argument << "\n";
        return 2;
    }

    if (input_path.empty() || output_path.empty()) {
        print_usage();
        return 2;
    }

    const auto source_text = read_text(input_path);
    if (!source_text.has_value()) {
        std::cerr << "Could not read mesh source manifest.\n";
        return 1;
    }
    const auto parsed = vice::tools::mesh_cooker::parse_source_manifest(*source_text);
    if (!parsed.source.has_value()) {
        std::cerr << parsed.diagnostics << "\n";
        return 1;
    }
    const auto cooked = vice::tools::mesh_cooker::cook(*parsed.source);
    if (!cooked.success) {
        std::cerr << cooked.diagnostics << "\n";
        return 1;
    }
    std::string diagnostics;
    if (!vice::tools::mesh_cooker::write_asset(output_path, cooked.bytes, &diagnostics)) {
        std::cerr << diagnostics << "\n";
        return 1;
    }

    if (!collision_output_path.empty()) {
#if VICE_MESH_COOKER_COLLISION
        using vice::tools::mesh_cooker::CollisionMode;
        if (parsed.source->collision == CollisionMode::none) {
            std::cerr << "Mesh source does not request collision output.\n";
            return 1;
        }
        vice::tools::collision_import::ImportedMesh collision_mesh;
        collision_mesh.source_key = parsed.source->name;
        collision_mesh.positions.reserve(parsed.source->vertices.size());
        for (const auto& vertex : parsed.source->vertices) {
            collision_mesh.positions.push_back(vice::math::Vec3{
                vertex.position[0], vertex.position[1], vertex.position[2],
            });
        }
        collision_mesh.indices = parsed.source->indices;
        collision_mesh.render_mesh = true;
        collision_mesh.collision.representation = parsed.source->collision == CollisionMode::triangle_mesh
            ? vice::tools::collision_import::CollisionRepresentation::triangle_mesh
            : vice::tools::collision_import::CollisionRepresentation::convex_hull;

        const std::array meshes{collision_mesh};
        const auto collision = vice::tools::collision_import::cook_collision(meshes);
        if (!collision.success) {
            std::cerr << collision.diagnostics << "\n";
            return 1;
        }
        if (!vice::tools::collision_cooker::write_asset(
                collision_output_path,
                collision.bytes,
                &diagnostics
            )) {
            std::cerr << diagnostics << "\n";
            return 1;
        }
#else
        std::cerr << "This mesh cooker build does not include collision cooking.\n";
        return 1;
#endif
    }

    std::cout
        << "cooked " << cooked.bytes.size() << " mesh bytes\n"
        << "fingerprint: 0x" << std::hex << std::setw(16) << std::setfill('0')
        << cooked.source_fingerprint << "\n"
        << "output: " << output_path.string() << "\n";
    return 0;
}
