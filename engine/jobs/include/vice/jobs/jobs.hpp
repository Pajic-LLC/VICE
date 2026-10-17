#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>

namespace vice::jobs {

enum class Priority : std::uint8_t {
    low = 0,
    normal,
    high,
    critical,
};

enum class Lane : std::uint8_t {
    worker,
    main_thread,
};

enum class Status : std::uint8_t {
    invalid,
    pending,
    queued,
    running,
    succeeded,
    failed,
};

struct SchedulerConfig {
    std::uint32_t worker_count = 0;
};

struct SchedulerStats {
    std::uint32_t worker_count = 0;
    std::size_t outstanding_tasks = 0;
    std::size_t queued_worker_tasks = 0;
    std::size_t queued_main_thread_tasks = 0;
    std::size_t running_tasks = 0;
    std::uint64_t total_submitted = 0;
    std::uint64_t total_completed = 0;
    std::uint64_t total_failed = 0;
};

struct TaskDesc {
    std::function<void()> function;
    Priority priority = Priority::normal;
    Lane lane = Lane::worker;
    std::string name;
};

enum class JobEventType : std::uint8_t {
    submitted,
    started,
    completed,
    failed,
};

struct JobEvent {
    JobEventType type = JobEventType::submitted;
    std::uint64_t id = 0;
    Priority priority = Priority::normal;
    Lane lane = Lane::worker;
    std::string name;
    std::thread::id thread_id;
};

using JobEventSink = std::function<void(const JobEvent&)>;

namespace detail {
struct TaskState;
}

class TaskHandle {
public:
    TaskHandle() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

private:
    explicit TaskHandle(std::shared_ptr<detail::TaskState> state) noexcept;

    std::shared_ptr<detail::TaskState> state_;

    friend class Scheduler;
};

class Scheduler {
public:
    explicit Scheduler(SchedulerConfig config = {});
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    [[nodiscard]] TaskHandle schedule(
        TaskDesc task,
        std::span<const TaskHandle> dependencies = {}
    );

    [[nodiscard]] Status wait(const TaskHandle& task);
    [[nodiscard]] std::size_t pump_main(std::size_t max_tasks = static_cast<std::size_t>(-1));

    void set_event_sink(JobEventSink sink);
    void shutdown();

    [[nodiscard]] std::uint32_t worker_count() const noexcept;
    [[nodiscard]] std::thread::id main_thread_id() const noexcept;
    [[nodiscard]] SchedulerStats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* priority_name(Priority priority) noexcept;
[[nodiscard]] const char* lane_name(Lane lane) noexcept;
[[nodiscard]] const char* status_name(Status status) noexcept;

} // namespace vice::jobs
