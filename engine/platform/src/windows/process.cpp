#include <vice/platform/process.hpp>

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace vice::platform {
namespace {

[[nodiscard]] bool utf8_to_wide(const std::string_view text, std::wstring& output) {
    output.clear();
    if (text.empty()) return true;

    const int input_size = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_size,
        nullptr,
        0
    );
    if (required <= 0) return false;

    output.resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_size,
        output.data(),
        required
    ) == required;
}

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring_view argument) {
    const bool needs_quotes = argument.empty() ||
        argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!needs_quotes) return std::wstring{argument};

    std::wstring result;
    result.push_back(L'\"');
    std::size_t backslashes = 0;
    for (const wchar_t value : argument) {
        if (value == L'\\') {
            ++backslashes;
            continue;
        }
        if (value == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(value);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::string windows_error(const DWORD code) {
    wchar_t* message = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr
    );
    if (size == 0U || message == nullptr) {
        return "Windows error " + std::to_string(code) + ".";
    }

    const std::wstring_view wide{message, size};
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    std::string result;
    if (required > 0) {
        result.resize(static_cast<std::size_t>(required));
        (void)WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.data(),
            static_cast<int>(wide.size()),
            result.data(),
            required,
            nullptr,
            nullptr
        );
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
            result.pop_back();
        }
    }
    LocalFree(message);
    return result.empty() ? "Windows error " + std::to_string(code) + "." : result;
}

[[nodiscard]] ProcessLaunchResult fail(std::string message) {
    return ProcessLaunchResult{.launched = false, .diagnostics = std::move(message)};
}

} // namespace

ProcessLaunchResult launch_process(const ProcessLaunchRequest& request) {
    if (request.executable.empty()) {
        return fail("Process executable path is empty.");
    }

    const std::wstring executable = request.executable.wstring();
    if (executable.empty()) {
        return fail("Process executable path could not be represented on Windows.");
    }

    std::wstring command_line = quote_windows_argument(executable);
    for (const auto& argument : request.arguments) {
        std::wstring wide_argument;
        if (!utf8_to_wide(argument, wide_argument)) {
            return fail("Process argument is not valid UTF-8.");
        }
        command_line.push_back(L' ');
        command_line += quote_windows_argument(wide_argument);
    }

    std::wstring working_directory;
    const wchar_t* working_directory_ptr = nullptr;
    if (!request.working_directory.empty()) {
        working_directory = request.working_directory.wstring();
        if (working_directory.empty()) {
            return fail("Process working directory could not be represented on Windows.");
        }
        working_directory_ptr = working_directory.c_str();
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(
        executable.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        working_directory_ptr,
        &startup,
        &process
    );
    if (launched == FALSE) {
        return fail("Could not launch process: " + windows_error(GetLastError()));
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ProcessLaunchResult{.launched = true};
}

} // namespace vice::platform
