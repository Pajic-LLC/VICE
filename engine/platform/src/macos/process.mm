#include <vice/platform/process.hpp>

#import <Foundation/Foundation.h>

#include <string>
#include <utility>

namespace vice::platform {
namespace {

[[nodiscard]] NSString* ns_string(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

[[nodiscard]] NSString* ns_path(const std::filesystem::path& value) {
    return ns_string(value.string());
}

[[nodiscard]] ProcessLaunchResult fail_launch(std::string message) {
    return ProcessLaunchResult{.launched = false, .diagnostics = std::move(message)};
}

[[nodiscard]] ProcessRunResult fail_run(std::string message) {
    return ProcessRunResult{.launched = false, .diagnostics = std::move(message)};
}

[[nodiscard]] bool configure_task(
    NSTask* task,
    const ProcessLaunchRequest& request,
    std::string& diagnostics
) {
    if (request.executable.empty()) {
        diagnostics = "Process executable path is empty.";
        return false;
    }

    NSString* const executable = ns_path(request.executable);
    if (executable == nil) {
        diagnostics = "Process executable path is not valid UTF-8.";
        return false;
    }

    NSMutableArray<NSString*>* arguments = [NSMutableArray arrayWithCapacity:request.arguments.size()];
    for (const auto& argument : request.arguments) {
        NSString* const value = ns_string(argument);
        if (value == nil) {
            diagnostics = "Process argument is not valid UTF-8.";
            return false;
        }
        [arguments addObject:value];
    }

    task.executableURL = [NSURL fileURLWithPath:executable];
    task.arguments = arguments;

    if (!request.working_directory.empty()) {
        NSString* const working_directory = ns_path(request.working_directory);
        if (working_directory == nil) {
            diagnostics = "Process working directory is not valid UTF-8.";
            return false;
        }
        task.currentDirectoryURL = [NSURL fileURLWithPath:working_directory isDirectory:YES];
    }

    diagnostics.clear();
    return true;
}

[[nodiscard]] std::string task_error(NSError* error) {
    if (error == nil) return "Could not launch process.";
    const char* const description = error.localizedDescription.UTF8String;
    return description != nullptr
        ? "Could not launch process: " + std::string{description}
        : "Could not launch process.";
}

[[nodiscard]] std::string data_text(NSData* data) {
    if (data == nil || data.length == 0U) return {};
    NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (text == nil) {
        text = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];
    }
    if (text == nil || text.UTF8String == nullptr) return {};
    return std::string{text.UTF8String};
}

} // namespace

ProcessLaunchResult launch_process(const ProcessLaunchRequest& request) {
    @autoreleasepool {
        NSTask* task = [[NSTask alloc] init];
        std::string diagnostics;
        if (!configure_task(task, request, diagnostics)) {
            return fail_launch(std::move(diagnostics));
        }

        if (!request.inherit_standard_streams) {
            NSFileHandle* null_device = [NSFileHandle fileHandleWithNullDevice];
            task.standardInput = null_device;
            task.standardOutput = null_device;
            task.standardError = null_device;
        }

        NSError* error = nil;
        if (![task launchAndReturnError:&error]) {
            return fail_launch(task_error(error));
        }
    }

    return ProcessLaunchResult{.launched = true};
}

ProcessRunResult run_process(const ProcessLaunchRequest& request) {
    @autoreleasepool {
        NSTask* task = [[NSTask alloc] init];
        std::string diagnostics;
        if (!configure_task(task, request, diagnostics)) {
            return fail_run(std::move(diagnostics));
        }

        NSPipe* pipe = [NSPipe pipe];
        task.standardOutput = pipe;
        task.standardError = pipe;

        NSError* error = nil;
        if (![task launchAndReturnError:&error]) {
            return fail_run(task_error(error));
        }

        NSData* const data = [[pipe fileHandleForReading] readDataToEndOfFile];
        [task waitUntilExit];

        ProcessRunResult result;
        result.launched = true;
        result.exit_code = task.terminationStatus;
        result.output = data_text(data);
        if (result.exit_code != 0) {
            result.diagnostics = "Process exited with code " + std::to_string(result.exit_code) + ".";
        }
        return result;
    }
}

} // namespace vice::platform
