#pragma once

#include <vice/jobs/jobs.hpp>

#include <Jolt/Core/JobSystemWithBarrier.h>

#include <mutex>
#include <vector>

namespace vice::rigid_body::detail {

class JoltJobSystem final : public JPH::JobSystemWithBarrier {
public:
    explicit JoltJobSystem(jobs::Scheduler& scheduler);
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

    jobs::Scheduler& scheduler_;
    std::mutex handles_mutex_;
    std::vector<jobs::TaskHandle> handles_;
};

} // namespace vice::rigid_body::detail
