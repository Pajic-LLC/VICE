#include <vice/jobs/jobs.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace vice::jobs {
namespace {

constexpr std::size_t priority_count = 4;

[[nodiscard]] constexpr std::size_t priority_index(const Priority priority) noexcept {
    return static_cast<std::size_t>(priority);
}

[[nodiscard]] bool terminal(const Status status) noexcept {
    return status == Status::succeeded || status == Status::failed;
}

std::atomic<std::uint64_t> next_scheduler_id{1};
thread_local void* current_scheduler = nullptr;

} // namespace

namespace detail {

struct TaskState {
    std::uint64_t scheduler_id = 0;
    std::uint64_t id = 0;
    TaskDesc task;
    std::atomic<Status> status{Status::pending};

    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    std::exception_ptr error;

    std::uint32_t remaining_dependencies = 0;
    std::vector<std::weak_ptr<TaskState>> dependents;
};

} // namespace detail

struct Scheduler::Impl {
    explicit Impl(const SchedulerConfig config)
        : scheduler_id{next_scheduler_id.fetch_add(1, std::memory_order_relaxed)},
          main_thread{std::this_thread::get_id()} {
        std::uint32_t desired_workers = config.worker_count;
        if (desired_workers == 0) {
            const std::uint32_t hardware_threads = std::thread::hardware_concurrency();
            desired_workers = hardware_threads > 1 ? hardware_threads - 1 : 1;
        }

        workers.reserve(desired_workers);
        for (std::uint32_t index = 0; index < desired_workers; ++index) {
            workers.emplace_back([this] {
                worker_loop();
            });
        }
    }

    ~Impl() {
        shutdown();
    }

    using QueueSet = std::array<std::deque<std::shared_ptr<detail::TaskState>>, priority_count>;

    [[nodiscard]] bool has_worker_work_locked() const noexcept {
        for (const auto& queue : worker_queues) {
            if (!queue.empty()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::shared_ptr<detail::TaskState> pop_highest_locked(QueueSet& queues) {
        for (std::size_t index = priority_count; index > 0; --index) {
            auto& queue = queues[index - 1];
            if (!queue.empty()) {
                auto task = std::move(queue.front());
                queue.pop_front();
                return task;
            }
        }
        return nullptr;
    }

    void emit(const JobEventType type, const std::shared_ptr<detail::TaskState>& state) {
        JobEventSink sink_copy;
        {
            std::lock_guard lock{event_mutex};
            sink_copy = event_sink;
        }

        if (!sink_copy) {
            return;
        }

        JobEvent event;
        event.type = type;
        event.id = state->id;
        event.priority = state->task.priority;
        event.lane = state->task.lane;
        event.name = state->task.name;
        event.thread_id = std::this_thread::get_id();
        sink_copy(event);
    }

    void enqueue(const std::shared_ptr<detail::TaskState>& state) {
        state->status.store(Status::queued, std::memory_order_release);

        {
            std::lock_guard lock{queue_mutex};
            auto& queues = state->task.lane == Lane::main_thread ? main_queues : worker_queues;
            queues[priority_index(state->task.priority)].push_back(state);
        }

        if (state->task.lane == Lane::worker) {
            queue_cv.notify_one();
        }
    }

    void complete_task(const std::shared_ptr<detail::TaskState>& state, const Status result) {
        state->status.store(result, std::memory_order_release);

        {
            std::lock_guard lock{state->completion_mutex};
        }
        state->completion_cv.notify_all();

        std::vector<std::shared_ptr<detail::TaskState>> ready;
        {
            std::lock_guard graph_lock{graph_mutex};
            for (auto& weak_dependent : state->dependents) {
                if (auto dependent = weak_dependent.lock()) {
                    if (dependent->remaining_dependencies > 0) {
                        --dependent->remaining_dependencies;
                    }
                    if (dependent->remaining_dependencies == 0) {
                        ready.push_back(std::move(dependent));
                    }
                }
            }
            state->dependents.clear();
        }

        for (const auto& dependent : ready) {
            enqueue(dependent);
        }

        const std::size_t previous = outstanding.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 1) {
            idle_cv.notify_all();
        }

        emit(result == Status::failed ? JobEventType::failed : JobEventType::completed, state);
    }

    void execute(const std::shared_ptr<detail::TaskState>& state) {
        state->status.store(Status::running, std::memory_order_release);
        emit(JobEventType::started, state);

        try {
            state->task.function();
            complete_task(state, Status::succeeded);
        } catch (...) {
            {
                std::lock_guard lock{state->completion_mutex};
                state->error = std::current_exception();
            }
            complete_task(state, Status::failed);
        }
    }

    void worker_loop() {
        current_scheduler = this;
        while (true) {
            std::shared_ptr<detail::TaskState> state;
            {
                std::unique_lock lock{queue_mutex};
                queue_cv.wait(lock, [this] {
                    return stopping || has_worker_work_locked();
                });

                if (stopping && !has_worker_work_locked()) {
                    current_scheduler = nullptr;
                    return;
                }

                state = pop_highest_locked(worker_queues);
            }

            if (state) {
                execute(state);
            }
        }
    }

    [[nodiscard]] TaskHandle schedule(
        TaskDesc task,
        const std::span<const TaskHandle> dependencies
    ) {
        if (!task.function || !accepting.load(std::memory_order_acquire)) {
            return {};
        }

        for (const auto& dependency : dependencies) {
            if (!dependency.state_ || dependency.state_->scheduler_id != scheduler_id) {
                return {};
            }
        }

        auto state = std::make_shared<detail::TaskState>();
        state->scheduler_id = scheduler_id;
        state->id = next_task_id.fetch_add(1, std::memory_order_relaxed);
        state->task = std::move(task);

        outstanding.fetch_add(1, std::memory_order_acq_rel);
        emit(JobEventType::submitted, state);

        bool ready = false;
        {
            std::lock_guard graph_lock{graph_mutex};
            for (const auto& dependency : dependencies) {
                const Status dependency_status = dependency.state_->status.load(std::memory_order_acquire);
                if (terminal(dependency_status)) {
                    continue;
                }

                ++state->remaining_dependencies;
                dependency.state_->dependents.push_back(state);
            }
            ready = state->remaining_dependencies == 0;
        }

        if (ready) {
            enqueue(state);
        }

        return TaskHandle{std::move(state)};
    }

    [[nodiscard]] std::size_t pump_main(const std::size_t max_tasks) {
        if (std::this_thread::get_id() != main_thread) {
            return 0;
        }

        std::size_t executed = 0;
        while (executed < max_tasks) {
            std::shared_ptr<detail::TaskState> state;
            {
                std::lock_guard lock{queue_mutex};
                state = pop_highest_locked(main_queues);
            }

            if (!state) {
                break;
            }

            execute(state);
            ++executed;
        }
        return executed;
    }

    [[nodiscard]] Status wait(const TaskHandle& handle) {
        if (!handle.state_ || handle.state_->scheduler_id != scheduler_id) {
            return Status::invalid;
        }

        while (!terminal(handle.state_->status.load(std::memory_order_acquire))) {
            bool helped = false;

            if (std::this_thread::get_id() == main_thread) {
                helped = pump_main(1) != 0;
            } else if (current_scheduler == this) {
                std::shared_ptr<detail::TaskState> ready;
                {
                    std::lock_guard lock{queue_mutex};
                    ready = pop_highest_locked(worker_queues);
                }
                if (ready) {
                    execute(ready);
                    helped = true;
                }
            }

            if (!helped) {
                std::unique_lock lock{handle.state_->completion_mutex};
                handle.state_->completion_cv.wait_for(lock, std::chrono::milliseconds{1}, [&handle] {
                    return terminal(handle.state_->status.load(std::memory_order_acquire));
                });
            }
        }

        return handle.state_->status.load(std::memory_order_acquire);
    }

    void shutdown() {
        bool expected = true;
        if (!accepting.compare_exchange_strong(
                expected,
                false,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            return;
        }

        while (outstanding.load(std::memory_order_acquire) != 0) {
            if (std::this_thread::get_id() == main_thread) {
                (void)pump_main(64);
            }

            std::unique_lock lock{idle_mutex};
            idle_cv.wait_for(lock, std::chrono::milliseconds{1}, [this] {
                return outstanding.load(std::memory_order_acquire) == 0;
            });
        }

        {
            std::lock_guard lock{queue_mutex};
            stopping = true;
        }
        queue_cv.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    const std::uint64_t scheduler_id;
    const std::thread::id main_thread;
    std::atomic<std::uint64_t> next_task_id{1};
    std::atomic<std::size_t> outstanding{0};
    std::atomic<bool> accepting{true};

    std::mutex graph_mutex;

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    QueueSet worker_queues;
    QueueSet main_queues;
    bool stopping = false;
    std::vector<std::thread> workers;

    std::mutex idle_mutex;
    std::condition_variable idle_cv;

    std::mutex event_mutex;
    JobEventSink event_sink;
};

TaskHandle::TaskHandle(std::shared_ptr<detail::TaskState> state) noexcept : state_{std::move(state)} {}

bool TaskHandle::valid() const noexcept {
    return state_ != nullptr;
}

std::uint64_t TaskHandle::id() const noexcept {
    return state_ ? state_->id : 0;
}

Status TaskHandle::status() const noexcept {
    return state_ ? state_->status.load(std::memory_order_acquire) : Status::invalid;
}

bool TaskHandle::complete() const noexcept {
    return terminal(status());
}

bool TaskHandle::failed() const noexcept {
    return status() == Status::failed;
}

Scheduler::Scheduler(const SchedulerConfig config) : impl_{std::make_unique<Impl>(config)} {}

Scheduler::~Scheduler() = default;

TaskHandle Scheduler::schedule(TaskDesc task, const std::span<const TaskHandle> dependencies) {
    return impl_->schedule(std::move(task), dependencies);
}

Status Scheduler::wait(const TaskHandle& task) {
    return impl_->wait(task);
}

std::size_t Scheduler::pump_main(const std::size_t max_tasks) {
    return impl_->pump_main(max_tasks);
}

void Scheduler::set_event_sink(JobEventSink sink) {
    std::lock_guard lock{impl_->event_mutex};
    impl_->event_sink = std::move(sink);
}

void Scheduler::shutdown() {
    impl_->shutdown();
}

std::uint32_t Scheduler::worker_count() const noexcept {
    return static_cast<std::uint32_t>(impl_->workers.size());
}

std::thread::id Scheduler::main_thread_id() const noexcept {
    return impl_->main_thread;
}

const char* priority_name(const Priority priority) noexcept {
    switch (priority) {
        case Priority::low:
            return "Low";
        case Priority::normal:
            return "Normal";
        case Priority::high:
            return "High";
        case Priority::critical:
            return "Critical";
    }
    return "Unknown";
}

const char* lane_name(const Lane lane) noexcept {
    switch (lane) {
        case Lane::worker:
            return "Worker";
        case Lane::main_thread:
            return "Main Thread";
    }
    return "Unknown";
}

const char* status_name(const Status status) noexcept {
    switch (status) {
        case Status::invalid:
            return "Invalid";
        case Status::pending:
            return "Pending";
        case Status::queued:
            return "Queued";
        case Status::running:
            return "Running";
        case Status::succeeded:
            return "Succeeded";
        case Status::failed:
            return "Failed";
    }
    return "Unknown";
}

} // namespace vice::jobs
