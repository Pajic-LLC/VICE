#pragma once

#include <vice/jobs/jobs.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <mutex>
#include <vector>

namespace vice::rigid_body::detail {

class SchedulerBinding final {
public:
    explicit SchedulerBinding(jobs::Scheduler& scheduler) noexcept;
    ~SchedulerBinding();

    SchedulerBinding(const SchedulerBinding&) = delete;
    SchedulerBinding& operator=(const SchedulerBinding&) = delete;
    SchedulerBinding(SchedulerBinding&&) = delete;
    SchedulerBinding& operator=(SchedulerBinding&&) = delete;

private:
    jobs::Scheduler* previous_ = nullptr;
};

class JoltJobSystem final : public JPH::JobSystemWithBarrier {
public:
    JoltJobSystem(JPH::uint max_jobs, JPH::uint max_barriers, int concurrency_hint);
    ~JoltJobSystem() override;

    JoltJobSystem(const JoltJobSystem&) = delete;
    JoltJobSystem& operator=(const JoltJobSystem&) = delete;
    JoltJobSystem(JoltJobSystem&&) = delete;
    JoltJobSystem& operator=(JoltJobSystem&&) = delete;

    [[nodiscard]] int GetMaxConcurrency() const override;
    [[nodiscard]] JobHandle CreateJob(
        const char* name,
        JPH::ColorArg color,
        const JobFunction& function,
        JPH::uint32 dependency_count = 0
    ) override;

    void QueueJob(Job* job) override;
    void QueueJobs(Job** jobs, JPH::uint job_count) override;
    void FreeJob(Job* job) override;
    void WaitForJobs(Barrier* barrier) override;

private:
    void queue(Job* job);
    void drain();

    jobs::Scheduler* scheduler_ = nullptr;
    int concurrency_hint_ = 0;
    std::mutex handles_mutex_;
    std::vector<jobs::TaskHandle> handles_;
};

} // namespace vice::rigid_body::detail
