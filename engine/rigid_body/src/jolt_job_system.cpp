#include "jolt_job_system.hpp"

#include <utility>

namespace vice::rigid_body::detail {

JoltJobSystem::JoltJobSystem(jobs::Scheduler& scheduler)
    : JPH::JobSystemWithBarrier{JPH::cMaxPhysicsBarriers},
      scheduler_{scheduler} {}

JoltJobSystem::~JoltJobSystem() {
    drain();
}

int JoltJobSystem::GetMaxConcurrency() const {
    return static_cast<int>(scheduler_.worker_count()) + 1;
}

JoltJobSystem::JobHandle JoltJobSystem::CreateJob(
    const char* name,
    const JPH::ColorArg color,
    const JobFunction& function,
    const JPH::uint32 dependency_count
) {
    auto* job = new Job{name, color, this, function, dependency_count};
    JobHandle handle{job};
    if (dependency_count == 0U) {
        QueueJob(job);
    }
    return handle;
}

void JoltJobSystem::QueueJob(Job* job) {
    queue(job);
}

void JoltJobSystem::QueueJobs(Job** jobs, const JPH::uint job_count) {
    for (JPH::uint index = 0; index < job_count; ++index) {
        queue(jobs[index]);
    }
}

void JoltJobSystem::FreeJob(Job* job) {
    delete job;
}

void JoltJobSystem::WaitForJobs(Barrier* barrier) {
    JPH::JobSystemWithBarrier::WaitForJobs(barrier);
    drain();
}

void JoltJobSystem::queue(Job* job) {
    job->AddRef();

    jobs::TaskDesc task;
    task.name = "jolt.physics";
    task.priority = jobs::Priority::high;
    task.lane = jobs::Lane::worker;
    task.function = [job] {
        (void)job->Execute();
        job->Release();
    };

    auto handle = scheduler_.schedule(std::move(task));
    if (!handle.valid()) {
        (void)job->Execute();
        job->Release();
        return;
    }

    std::scoped_lock lock{handles_mutex_};
    handles_.push_back(std::move(handle));
}

void JoltJobSystem::drain() {
    for (;;) {
        std::vector<jobs::TaskHandle> handles;
        {
            std::scoped_lock lock{handles_mutex_};
            if (handles_.empty()) {
                return;
            }
            handles.swap(handles_);
        }

        for (const auto& handle : handles) {
            (void)scheduler_.wait(handle);
        }
    }
}

} // namespace vice::rigid_body::detail
