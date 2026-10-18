#include <vice/platform/process.hpp>

#include <windows.h>

#include <array>
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

struct PreparedProcess {
    std::wstring executable;
    std::wstring command_line;
    std::wstring working_directory;
};

[[nodiscard]] bool prepare_process(
    const ProcessLaunchRequest& request,
    PreparedProcess& prepared,
    std::string& diagnostics
) {
    if (request.executable.empty()) {
        diagnostics = "Process executable path is empty.";
        return false;
    }

    prepared.executable = request.executable.wstring();
    if (prepared.executable.empty()) {
        diagnostics = "Process executable path could not be represented on Windows.";
        return false;
    }

    prepared.command_line = quote_windows_argument(prepared.executable);
    for (const auto& argument : request.arguments) {
        std::wstring wide_argument;
        if (!utf8_to_wide(argument, wide_argument)) {
            diagnostics = "Process argument is not valid UTF-8.";
            return false;
        }
        prepared.command_line.push_back(L' ');
        prepared.command_line += quote_windows_argument(wide_argument);
    }

    prepared.working_directory.clear();
    if (!request.working_directory.empty()) {
        prepared.working_directory = request.working_directory.wstring();
        if (prepared.working_directory.empty()) {
            diagnostics = "Process working directory could not be represented on Windows.";
            return false;
        }
    }

    diagnostics.clear();
    return true;
}

[[nodiscard]] ProcessLaunchResult fail_launch(std::string message) {
    return ProcessLaunchResult{.launched = false, .diagnostics = std::move(message)};
}

[[nodiscard]] ProcessRunResult fail_run(std::string message) {
    return ProcessRunResult{.launched = false, .diagnostics = std::move(message)};
}

} // namespace

ProcessLaunchResult launch_process(const ProcessLaunchRequest& request) {
    PreparedProcess prepared;
    std::string diagnostics;
    if (!prepare_process(request, prepared, diagnostics)) {
        return fail_launch(std::move(diagnostics));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const wchar_t* const working_directory = prepared.working_directory.empty()
        ? nullptr
        : prepared.working_directory.c_str();
    const BOOL launched = CreateProcessW(
        prepared.executable.c_str(),
        prepared.command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        working_directory,
        &startup,
        &process
    );
    if (launched == FALSE) {
        return fail_launch("Could not launch process: " + windows_error(GetLastError()));
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ProcessLaunchResult{.launched = true};
}

ProcessRunResult run_process(const ProcessLaunchRequest& request) {
    PreparedProcess prepared;
    std::string diagnostics;
    if (!prepare_process(request, prepared, diagnostics)) {
        return fail_run(std::move(diagnostics));
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (CreatePipe(&output_read, &output_write, &security, 0) == FALSE) {
        return fail_run("Could not create process output pipe: " + windows_error(GetLastError()));
    }
    if (SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0) == FALSE) {
        const std::string message = windows_error(GetLastError());
        CloseHandle(output_read);
        CloseHandle(output_write);
        return fail_run("Could not configure process output pipe: " + message);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;

    PROCESS_INFORMATION process{};
    const wchar_t* const working_directory = prepared.working_directory.empty()
        ? nullptr
        : prepared.working_directory.c_str();
    const BOOL launched = CreateProcessW(
        prepared.executable.c_str(),
        prepared.command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory,
        &startup,
        &process
    );
    if (launched == FALSE) {
        const std::string message = windows_error(GetLastError());
        CloseHandle(output_read);
        CloseHandle(output_write);
        return fail_run("Could not launch process: " + message);
    }

    CloseHandle(output_write);
    output_write = nullptr;
    CloseHandle(process.hThread);

    ProcessRunResult result;
    result.launched = true;

    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD read = 0;
        const BOOL read_ok = ReadFile(
            output_read,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &read,
            nullptr
        );
        if (read_ok != FALSE && read > 0U) {
            result.output.append(buffer.data(), static_cast<std::size_t>(read));
            continue;
        }
        if (read_ok == FALSE) {
            const DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE) {
                result.diagnostics = "Could not read process output: " + windows_error(error);
            }
        }
        break;
    }
    CloseHandle(output_read);

    const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
    if (wait != WAIT_OBJECT_0) {
        if (result.diagnostics.empty()) {
            result.diagnostics = "Could not wait for process: " + windows_error(GetLastError());
        }
        CloseHandle(process.hProcess);
        result.exit_code = -1;
        return result;
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
        if (result.diagnostics.empty()) {
            result.diagnostics = "Could not read process exit code: " + windows_error(GetLastError());
        }
        CloseHandle(process.hProcess);
        result.exit_code = -1;
        return result;
    }
    CloseHandle(process.hProcess);

    result.exit_code = static_cast<int>(exit_code);
    if (result.exit_code != 0 && result.diagnostics.empty()) {
        result.diagnostics = "Process exited with code " + std::to_string(result.exit_code) + ".";
    }
    return result;
}

} // namespace vice::platform
