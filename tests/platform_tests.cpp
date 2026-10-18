#include <vice/platform/dynamic_library.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/platform/process.hpp>
#include <vice/platform/window.hpp>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

int main(const int argc, char** argv) {
    using namespace vice::platform;

    if (argc == 2 && std::string{argv[1]} == "--vice-process-child") {
        std::cout << "VICE process child output\n";
        return 7;
    }

    const WindowMetrics metrics{1280, 720, 2560, 1440, 2.0F};
    const Event resize{WindowMetricsChanged{7, metrics}};
    assert(std::holds_alternative<WindowMetricsChanged>(resize.payload));

    const auto& changed = std::get<WindowMetricsChanged>(resize.payload);
    assert(changed.window == 7);
    assert(changed.metrics == metrics);

    ModifierMask modifiers = modifier_mask(Modifier::shift);
    modifiers |= modifier_mask(Modifier::control);
    assert(has_modifier(modifiers, Modifier::shift));
    assert(has_modifier(modifiers, Modifier::control));
    assert(!has_modifier(modifiers, Modifier::alt));

    const Event key{KeyEvent{7, Key::escape, 53, modifiers, true, false}};
    const auto& key_event = std::get<KeyEvent>(key.payload);
    assert(key_event.window == 7);
    assert(key_event.key == Key::escape);
    assert(key_event.pressed);

    const NativeWindowHandle handle{};
    assert(handle.system == NativeWindowSystem::unknown);
    assert(handle.window == nullptr);
    assert(handle.view == nullptr);

    const auto invalid_process = launch_process(ProcessLaunchRequest{});
    assert(!invalid_process);
    assert(!invalid_process.diagnostics.empty());

    const auto invalid_run = run_process(ProcessLaunchRequest{});
    assert(!invalid_run);
    assert(!invalid_run.launched);
    assert(!invalid_run.diagnostics.empty());

    const auto child_executable = std::filesystem::absolute(std::filesystem::path{argv[0]}).lexically_normal();
    ProcessLaunchRequest child_request;
    child_request.executable = child_executable;
    child_request.arguments.emplace_back("--vice-process-child");
    child_request.working_directory = child_executable.parent_path();
    const auto child = run_process(child_request);
    assert(child.launched);
    assert(child.exit_code == 7);
    assert(!child);
    assert(child.output.find("VICE process child output") != std::string::npos);
    assert(!child.diagnostics.empty());

    std::string library_diagnostics;
    const auto invalid_library = DynamicLibrary::load({}, &library_diagnostics);
    assert(!invalid_library);
    assert(!library_diagnostics.empty());

    return 0;
}
