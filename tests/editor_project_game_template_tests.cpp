#include <vice/editor/project_session.hpp>
#include <vice/editor/project_workflow.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project game template test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    require(static_cast<bool>(input), "starter game source opens");
    std::ostringstream output;
    output << input.rdbuf();
    require(input.eof() || !input.fail(), "starter game source reads");
    return output.str();
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_editor_project_game_template_test_47000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    require(!error, "temporary project parent");

    editor::ProjectCreateRequest request;
    request.name = "Module Starter";
    request.parent_directory = root;
    const auto created = editor::create_project(request);
    require(static_cast<bool>(created), "new project creation");

    const auto game_source = root / "Module Starter" / "game" / "game.cpp";
    require(std::filesystem::is_regular_file(game_source), "new project owns game/game.cpp");
    const std::string source = read_text(game_source);
    require(
        source.find("#include <vice/runtime/module_api.hpp>") != std::string::npos,
        "starter includes public module API"
    );
    require(source.find("class Game final") != std::string::npos, "starter has Game class");
    require(source.find("VICE_GAME_MODULE(Game)") != std::string::npos, "starter exports game module");
    require(source.find("Game started.") != std::string::npos, "starter startup callback is readable");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
