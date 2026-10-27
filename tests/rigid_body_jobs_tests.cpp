#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

int main() {
    using namespace vice;

    jobs::Scheduler scheduler{jobs::SchedulerConfig{.worker_count = 2}};
    std::atomic<std::size_t> submitted{0};
    std::atomic<std::size_t> started{0};
    std::atomic<std::size_t> completed{0};

    scheduler.set_event_sink([&](const jobs::JobEvent& event) {
        if (event.name != std::string_view{"jolt.physics"}) {
            return;
        }
        if (event.type == jobs::JobEventType::submitted) {
            submitted.fetch_add(1, std::memory_order_relaxed);
        } else if (event.type == jobs::JobEventType::started) {
            started.fetch_add(1, std::memory_order_relaxed);
        } else if (event.type == jobs::JobEventType::completed) {
            completed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    {
        rigid_body::DomainConfig config;
        config.worker_threads = 2;
        auto domain = rigid_body::Domain::create(
            simulation::DomainId{900},
            "tests.jobs",
            scheduler,
            config
        );
        assert(domain);

        rigid_body::BoxBodyDesc floor;
        floor.participant = simulation::ParticipantId{1};
        floor.motion = rigid_body::MotionType::static_body;
        floor.half_extents = math::Vec3{20.0F, 0.5F, 20.0F};
        floor.position = math::Vec3{0.0F, -0.5F, 0.0F};
        assert(domain->create_box(floor));

        constexpr std::size_t body_count = 96;
        for (std::size_t index = 0; index < body_count; ++index) {
            rigid_body::BoxBodyDesc body;
            body.participant = simulation::ParticipantId{static_cast<std::uint64_t>(index + 2U)};
            body.motion = rigid_body::MotionType::dynamic_body;
            body.half_extents = math::Vec3{0.25F, 0.25F, 0.25F};
            body.position = math::Vec3{
                static_cast<float>(index % 8U) * 0.7F,
                1.0F + static_cast<float>(index / 8U) * 0.6F,
                static_cast<float>((index / 4U) % 8U) * 0.7F,
            };
            assert(domain->create_box(body));
        }

        simulation::Coordinator coordinator{1.0 / 120.0};
        assert(coordinator.register_domain(*domain));
        for (std::size_t step = 0; step < 30U; ++step) {
            coordinator.step();
        }
        assert(coordinator.unregister_domain(domain->id()));

    }

    // Task completion can wake a waiter before its instrumentation callback runs.
    // Join workers before asserting that all completion events were delivered.
    scheduler.shutdown();
    assert(submitted.load(std::memory_order_relaxed) > 0U);
    assert(started.load(std::memory_order_relaxed) > 0U);
    assert(completed.load(std::memory_order_relaxed) > 0U);
    assert(completed.load(std::memory_order_relaxed) == submitted.load(std::memory_order_relaxed));
    return 0;
}
