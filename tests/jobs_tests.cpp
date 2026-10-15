#include <vice/jobs/jobs.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

bool check(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace vice::jobs;

    bool ok = true;
    Scheduler scheduler{SchedulerConfig{4}};

    std::atomic<int> count{0};
    std::vector<TaskHandle> batch;
    batch.reserve(512);
    for (int index = 0; index < 512; ++index) {
        TaskDesc task;
        task.function = [&count] {
            count.fetch_add(1, std::memory_order_relaxed);
        };
        batch.push_back(scheduler.schedule(std::move(task)));
    }
    for (const auto& task : batch) {
        ok &= check(scheduler.wait(task) == Status::succeeded, "batch task should succeed");
    }
    ok &= check(count.load(std::memory_order_relaxed) == 512, "all batch tasks should execute");

    std::mutex order_mutex;
    std::vector<int> order;

    TaskDesc first_desc;
    first_desc.function = [&] {
        std::lock_guard lock{order_mutex};
        order.push_back(1);
    };
    const auto first = scheduler.schedule(std::move(first_desc));

    TaskDesc second_desc;
    second_desc.function = [&] {
        std::lock_guard lock{order_mutex};
        order.push_back(2);
    };
    const std::array first_dependency{first};
    const auto second = scheduler.schedule(
        std::move(second_desc),
        std::span<const TaskHandle>{first_dependency}
    );

    TaskDesc third_desc;
    third_desc.function = [&] {
        std::lock_guard lock{order_mutex};
        order.push_back(3);
    };
    const std::array second_dependency{second};
    const auto third = scheduler.schedule(
        std::move(third_desc),
        std::span<const TaskHandle>{second_dependency}
    );

    ok &= check(scheduler.wait(third) == Status::succeeded, "dependency chain should complete");
    {
        std::lock_guard lock{order_mutex};
        ok &= check(order == std::vector<int>({1, 2, 3}), "dependency order should be preserved");
    }

    std::atomic<int> fan_value{0};
    TaskDesc root_desc;
    root_desc.function = [&] {
        fan_value.store(1, std::memory_order_release);
    };
    const auto root = scheduler.schedule(std::move(root_desc));

    std::vector<TaskHandle> children;
    children.reserve(16);
    const std::array root_dependency{root};
    for (int index = 0; index < 16; ++index) {
        TaskDesc child_desc;
        child_desc.function = [&] {
            fan_value.fetch_add(1, std::memory_order_acq_rel);
        };
        children.push_back(scheduler.schedule(
            std::move(child_desc),
            std::span<const TaskHandle>{root_dependency}
        ));
    }

    std::atomic<bool> joined{false};
    TaskDesc join_desc;
    join_desc.function = [&] {
        joined.store(fan_value.load(std::memory_order_acquire) == 17, std::memory_order_release);
    };
    const auto join = scheduler.schedule(
        std::move(join_desc),
        std::span<const TaskHandle>{children}
    );
    ok &= check(scheduler.wait(join) == Status::succeeded, "fan-in job should succeed");
    ok &= check(joined.load(std::memory_order_acquire), "fan-in should run after every child");

    std::vector<int> main_order;
    TaskDesc normal_main;
    normal_main.lane = Lane::main_thread;
    normal_main.priority = Priority::normal;
    normal_main.function = [&] {
        main_order.push_back(1);
    };
    const auto normal = scheduler.schedule(std::move(normal_main));

    TaskDesc critical_main;
    critical_main.lane = Lane::main_thread;
    critical_main.priority = Priority::critical;
    critical_main.function = [&] {
        main_order.push_back(2);
    };
    const auto critical = scheduler.schedule(std::move(critical_main));

    ok &= check(scheduler.pump_main(2) == 2, "main-thread queue should execute two tasks");
    ok &= check(main_order == std::vector<int>({2, 1}), "main-thread priority should be respected");
    ok &= check(normal.complete() && critical.complete(), "main-thread tasks should complete");

    std::atomic<bool> main_dependency_ran{false};
    TaskDesc background_desc;
    background_desc.function = [] {};
    const auto background = scheduler.schedule(std::move(background_desc));

    TaskDesc main_dependency_desc;
    main_dependency_desc.lane = Lane::main_thread;
    main_dependency_desc.function = [&] {
        main_dependency_ran.store(
            std::this_thread::get_id() == scheduler.main_thread_id(),
            std::memory_order_release
        );
    };
    const std::array background_dependency{background};
    const auto main_dependency = scheduler.schedule(
        std::move(main_dependency_desc),
        std::span<const TaskHandle>{background_dependency}
    );

    ok &= check(
        scheduler.wait(main_dependency) == Status::succeeded,
        "owner-thread wait should pump ready main-thread work"
    );
    ok &= check(
        main_dependency_ran.load(std::memory_order_acquire),
        "main-thread lane should execute on scheduler owner thread"
    );

    {
        Scheduler one_worker{SchedulerConfig{1}};
        std::atomic<bool> nested_wait_completed{false};

        TaskDesc parent_desc;
        parent_desc.function = [&] {
            TaskDesc child_desc;
            child_desc.function = [&] {
                nested_wait_completed.store(true, std::memory_order_release);
            };
            const auto child = one_worker.schedule(std::move(child_desc));
            if (one_worker.wait(child) != Status::succeeded) {
                nested_wait_completed.store(false, std::memory_order_release);
            }
        };

        const auto parent = one_worker.schedule(std::move(parent_desc));
        ok &= check(
            one_worker.wait(parent) == Status::succeeded,
            "worker wait should help execute ready jobs instead of deadlocking"
        );
        ok &= check(
            nested_wait_completed.load(std::memory_order_acquire),
            "nested worker wait should complete with one worker"
        );
    }

    TaskDesc failing_desc;
    failing_desc.function = [] {
        throw std::runtime_error{"expected job failure"};
    };
    const auto failing = scheduler.schedule(std::move(failing_desc));
    ok &= check(scheduler.wait(failing) == Status::failed, "exceptions should mark a task failed");
    ok &= check(failing.failed(), "failed task should report failure");

    TaskDesc after_failure_desc;
    after_failure_desc.function = [] {};
    const auto after_failure = scheduler.schedule(std::move(after_failure_desc));
    ok &= check(
        scheduler.wait(after_failure) == Status::succeeded,
        "worker pool should survive a task exception"
    );

    return ok ? 0 : 1;
}
