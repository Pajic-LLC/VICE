#include <vice/editor/profiler.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/telemetry.hpp>
#include <vice/simulation/simulation.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

class TestDomain final : public vice::simulation::Domain {
public:
    TestDomain(
        const std::uint64_t id,
        std::string name,
        const vice::simulation::Fidelity fidelity
    ) : id_{id}, name_{std::move(name)}, fidelity_{fidelity} {}

    [[nodiscard]] vice::simulation::DomainId id() const noexcept override { return id_; }
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] vice::simulation::Fidelity fidelity() const noexcept override { return fidelity_; }
    void set_fidelity(const vice::simulation::Fidelity fidelity) noexcept override { fidelity_ = fidelity; }

    void step(
        const vice::simulation::StepContext&,
        vice::simulation::InteractionEmitter&
    ) override {
        ++steps_;
    }

    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }

private:
    vice::simulation::DomainId id_{};
    std::string name_;
    vice::simulation::Fidelity fidelity_ = vice::simulation::Fidelity::off;
    std::uint64_t steps_ = 0;
};

void test_scheduler_stats() {
    using namespace vice;

    jobs::Scheduler scheduler{jobs::SchedulerConfig{.worker_count = 1}};
    auto stats = scheduler.stats();
    assert(stats.worker_count == 1U);
    assert(stats.outstanding_tasks == 0U);
    assert(stats.total_submitted == 0U);

    auto started_promise = std::make_shared<std::promise<void>>();
    auto started = started_promise->get_future();
    auto release_promise = std::make_shared<std::promise<void>>();
    auto release = release_promise->get_future().share();
    auto running = scheduler.schedule(jobs::TaskDesc{
        .function = [started_promise, release] {
            started_promise->set_value();
            release.wait();
        },
        .name = "telemetry-running",
    });
    assert(running.valid());
    started.wait();

    stats = scheduler.stats();
    assert(stats.running_tasks == 1U);
    assert(stats.outstanding_tasks == 1U);
    assert(stats.total_submitted == 1U);

    release_promise->set_value();
    assert(scheduler.wait(running) == jobs::Status::succeeded);

    auto failing = scheduler.schedule(jobs::TaskDesc{
        .function = [] { throw std::runtime_error{"expected telemetry failure"}; },
        .name = "telemetry-failure",
    });
    assert(failing.valid());
    assert(scheduler.wait(failing) == jobs::Status::failed);

    auto main_task = scheduler.schedule(jobs::TaskDesc{
        .function = [] {},
        .lane = jobs::Lane::main_thread,
        .name = "telemetry-main",
    });
    assert(main_task.valid());
    stats = scheduler.stats();
    assert(stats.queued_main_thread_tasks == 1U);
    assert(stats.outstanding_tasks == 1U);
    assert(scheduler.pump_main(1U) == 1U);
    assert(main_task.status() == jobs::Status::succeeded);

    auto dependency = scheduler.schedule(jobs::TaskDesc{
        .function = [] {},
        .lane = jobs::Lane::main_thread,
        .name = "telemetry-dependency",
    });
    assert(dependency.valid());
    const std::array<jobs::TaskHandle, 1> dependencies{dependency};
    auto dependent = scheduler.schedule(
        jobs::TaskDesc{
            .function = [] {},
            .lane = jobs::Lane::main_thread,
            .name = "telemetry-dependent",
        },
        dependencies
    );
    assert(dependent.valid());

    stats = scheduler.stats();
    assert(stats.outstanding_tasks == 2U);
    assert(stats.queued_main_thread_tasks == 1U);
    assert(dependent.status() == jobs::Status::pending);

    assert(scheduler.pump_main(1U) == 1U);
    stats = scheduler.stats();
    assert(dependency.status() == jobs::Status::succeeded);
    assert(dependent.status() == jobs::Status::queued);
    assert(stats.outstanding_tasks == 1U);
    assert(stats.queued_main_thread_tasks == 1U);

    assert(scheduler.pump_main(1U) == 1U);
    assert(dependent.status() == jobs::Status::succeeded);

    stats = scheduler.stats();
    assert(stats.running_tasks == 0U);
    assert(stats.outstanding_tasks == 0U);
    assert(stats.queued_worker_tasks == 0U);
    assert(stats.queued_main_thread_tasks == 0U);
    assert(stats.total_submitted == 5U);
    assert(stats.total_completed == 5U);
    assert(stats.total_failed == 1U);
}

void test_simulation_stats() {
    using namespace vice;

    simulation::Coordinator coordinator{1.0 / 120.0};
    TestDomain full{1U, "full", simulation::Fidelity::full};
    TestDomain reduced{2U, "reduced", simulation::Fidelity::reduced};
    TestDomain off{3U, "off", simulation::Fidelity::off};
    assert(coordinator.register_domain(full));
    assert(coordinator.register_domain(reduced));
    assert(coordinator.register_domain(off));

    auto stats = coordinator.stats();
    assert(stats.registered_domains == 3U);
    assert(stats.full_domains == 1U);
    assert(stats.reduced_domains == 1U);
    assert(stats.off_domains == 1U);
    assert(stats.step_index == 0U);

    coordinator.step();
    stats = coordinator.stats();
    assert(stats.step_index == 1U);
    assert(stats.fixed_delta_seconds == 1.0 / 120.0);
    assert(stats.last_step_wall_seconds >= 0.0);
    assert(stats.recent_step_average_seconds >= 0.0);
    assert(full.steps() == 1U);
    assert(reduced.steps() == 1U);
    assert(off.steps() == 0U);

    off.set_fidelity(simulation::Fidelity::full);
    stats = coordinator.stats();
    assert(stats.full_domains == 2U);
    assert(stats.reduced_domains == 1U);
    assert(stats.off_domains == 0U);
}

void test_profiler_history_and_world_summary() {
    using namespace vice;

    editor::ProfilerHistory history{3U};
    history.push_frame(0.010);
    history.push_frame(0.020);
    history.push_frame(0.030);
    history.push_frame(0.040);
    history.push_frame(-1.0);
    const auto samples = history.frame_milliseconds();
    assert(samples.size() == 3U);
    assert(std::abs(samples[0] - 20.0F) < 0.01F);
    assert(std::abs(samples[1] - 30.0F) < 0.01F);
    assert(std::abs(samples[2] - 40.0F) < 0.01F);

    world::World world;
    for (std::uint64_t index = 1; index <= 3; ++index) {
        world::RegionDesc region;
        region.id = world::RegionId{index};
        region.simulation_island = world::SimulationIslandId{index};
        region.streaming_cell = {static_cast<std::int32_t>(index), 0, 0};
        region.fidelity = index == 1U
            ? world::RegionFidelity::full
            : (index == 2U ? world::RegionFidelity::reduced : world::RegionFidelity::inactive);
        assert(world.register_region(region));
    }

    const auto first = world.create_entity(fixed_id(1));
    const auto second = world.create_entity(fixed_id(2));
    assert(first.has_value());
    assert(second.has_value());
    const world::ResourceId resource{fixed_id(3)};
    assert(world.create_resource(resource).has_value());
    assert(world.add_relation(world::Relation{
        .subject = *first,
        .kind = fixed_id(4),
        .object = *second,
    }));

    const auto summary = editor::summarize_world(world);
    assert(summary.entities == 2U);
    assert(summary.resources == 1U);
    assert(summary.relations == 1U);
    assert(summary.regions == 3U);
    assert(summary.full_regions == 1U);
    assert(summary.reduced_regions == 1U);
    assert(summary.inactive_regions == 1U);
}

void test_render_device_stats() {
    using namespace vice;

    auto device = render::instrument_device(render::Device::create());
    assert(device != nullptr);
    assert(device->valid());

    const auto initial = device->stats();
    assert(initial.created_buffers == 0U);
    assert(initial.submitted_command_lists == 0U);

    render::BufferDesc buffer_desc;
    buffer_desc.size = 64U;
    buffer_desc.usage = render::BufferUsage::copy_source;
    buffer_desc.memory = render::MemoryLocation::cpu_to_gpu;
    auto buffer = device->create_buffer(buffer_desc);
    assert(buffer != nullptr);

    render::TextureDesc texture_desc;
    texture_desc.width = 4U;
    texture_desc.height = 4U;
    texture_desc.usage = render::TextureUsage::sampled;
    auto texture = device->create_texture(texture_desc);
    assert(texture != nullptr);

    auto sampler = device->create_sampler(render::SamplerDesc{});
    assert(sampler != nullptr);

    auto commands = device->create_command_list();
    assert(commands != nullptr);
    const auto submission = device->submit(std::move(commands));
    assert(submission != render::invalid_submission);

    const auto stats = device->stats();
    assert(stats.created_buffers == 1U);
    assert(stats.created_textures == 1U);
    assert(stats.created_samplers == 1U);
    assert(stats.submitted_command_lists == 1U);
    assert(stats.completed_submission >= submission);
}

} // namespace

int main() {
    test_scheduler_stats();
    test_simulation_stats();
    test_profiler_history_and_world_summary();
    test_render_device_stats();
    return 0;
}
