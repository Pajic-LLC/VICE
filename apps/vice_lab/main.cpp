#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

namespace {

struct LabVertex {
    float position[2];
    float color[3];
};

static_assert(sizeof(LabVertex) == 20U);

static constexpr std::array<LabVertex, 3> triangle_vertices{{
    {{0.0F, 0.72F}, {0.20F, 0.84F, 1.00F}},
    {{-0.68F, -0.58F}, {1.00F, 0.26F, 0.62F}},
    {{0.68F, -0.58F}, {0.98F, 0.82F, 0.18F}},
}};

static constexpr std::array<std::uint16_t, 3> triangle_indices{0U, 1U, 2U};

static constexpr std::array triangle_attributes{
    vice::render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = vice::render::VertexFormat::float32x2,
        .offset = 0,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = vice::render::VertexFormat::float32x3,
        .offset = 8,
    },
};

static constexpr std::array triangle_vertex_layouts{
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(LabVertex),
        .step_mode = vice::render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = triangle_attributes,
    },
};

constexpr std::string_view metal_triangle_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(11)]];
    float3 color [[attribute(12)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vice_triangle_vs(VertexIn input [[stage_in]]) {
    VertexOut out;
    out.position = float4(input.position, 0.0, 1.0);
    out.color = input.color;
    return out;
}

fragment float4 vice_triangle_fs(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
)";

constexpr std::string_view hlsl_triangle_source = R"(
struct VertexIn {
    float2 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_triangle_vs(VertexIn input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

float4 vice_triangle_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] std::unique_ptr<vice::render::ShaderModule> load_lab_shader(
    vice::render::Device& device,
    const std::filesystem::path& cooked_directory,
    const vice::render::ShaderModuleDesc& development_fallback
) {
    const std::filesystem::path manifest = cooked_directory / "shader.vice.json";
    std::error_code error;
    const bool manifest_exists = std::filesystem::exists(manifest, error);
    if (error) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab could not inspect cooked shader manifest: " + error.message()
        );
        return nullptr;
    }

    if (manifest_exists) {
        auto loaded = vice::render::load_cooked_shader(device, manifest);
        if (!loaded) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Lab cooked shader load failed: " + loaded.diagnostics
            );
            return nullptr;
        }
        vice::core::log(
            vice::core::LogLevel::debug,
            "VICE Lab loaded cooked shader: " + cooked_directory.filename().string()
        );
        return std::move(loaded.module);
    }

    vice::core::log(
        vice::core::LogLevel::debug,
        "VICE Lab cooked shader is absent; using development source fallback: " +
            cooked_directory.filename().string()
    );
    return device.create_shader_module(development_fallback);
}

} // namespace

int main() {
    using namespace vice::core;

    const auto platform = current_platform();
    if (platform == Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE development target.");
        return 1;
    }

    log(LogLevel::info, std::string{"VICE "} + std::string{version_string()});
    log(LogLevel::info, std::string{"Platform: "} + std::string{platform_name(platform)});
    log(LogLevel::info, std::string{"Architecture: "} + std::string{architecture_name()});
    log(LogLevel::info, std::string{"Compiler: "} + std::string{compiler_name()});
    log(
        LogLevel::info,
        std::string{"Build: "} + std::string{build_configuration_name(current_build_configuration())}
    );

    vice::jobs::Scheduler jobs;
    log(LogLevel::info, std::string{"Job workers: "} + std::to_string(jobs.worker_count()));

    std::atomic<bool> background_ready{false};
    vice::jobs::TaskDesc background_startup;
    background_startup.name = "vice_lab.background_startup";
    background_startup.priority = vice::jobs::Priority::high;
    background_startup.function = [&] {
        background_ready.store(true, std::memory_order_release);
    };
    const auto background_job = jobs.schedule(std::move(background_startup));

    vice::jobs::TaskDesc main_startup;
    main_startup.name = "vice_lab.main_startup";
    main_startup.lane = vice::jobs::Lane::main_thread;
    main_startup.function = [&] {
        if (background_ready.load(std::memory_order_acquire)) {
            log(LogLevel::debug, "VICE Lab job dependency completed.");
        }
    };
    const std::array startup_dependencies{background_job};
    const auto main_job = jobs.schedule(
        std::move(main_startup),
        std::span<const vice::jobs::TaskHandle>{startup_dependencies}
    );
    if (jobs.wait(main_job) != vice::jobs::Status::succeeded) {
        log(LogLevel::error, "VICE Lab job system startup check failed.");
        return 1;
    }

    auto platform_services = vice::platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE could not initialize platform services.");
        return 1;
    }

    const auto paths = platform_services->paths();
    log(LogLevel::debug, std::string{"Executable: "} + paths.executable.string());
    log(LogLevel::debug, std::string{"User data: "} + paths.user_data.string());

    vice::platform::WindowDesc window_desc;
    window_desc.title = "VICE Lab";
    window_desc.width = 1280;
    window_desc.height = 720;

    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Lab could not create a native window.");
        return 1;
    }

    const auto metrics = window->metrics();
    log(
        LogLevel::info,
        std::string{"Window: "} + std::to_string(metrics.width) + "x" +
            std::to_string(metrics.height) + " logical, " +
            std::to_string(metrics.drawable_width) + "x" +
            std::to_string(metrics.drawable_height) + " drawable"
    );

    auto render_device = vice::render::Device::create();
    if (!render_device) {
        log(LogLevel::error, "VICE Lab could not create a render device.");
        return 1;
    }

    const auto render_capabilities = render_device->capabilities();
    log(
        LogLevel::info,
        std::string{"Renderer: "} + std::string{vice::render::backend_name(render_device->backend())}
    );
    log(
        LogLevel::debug,
        std::string{"RHI unified memory: "} + (render_capabilities.unified_memory ? "yes" : "no")
    );

    std::array<std::byte, 64> probe_source{};
    vice::jobs::TaskDesc build_probe;
    build_probe.name = "vice_lab.build_rhi_probe";
    build_probe.function = [&] {
        for (std::size_t index = 0; index < probe_source.size(); ++index) {
            probe_source[index] = std::byte{
                static_cast<unsigned char>((index * 17U + 11U) & 0xFFU)
            };
        }
    };
    const auto probe_job = jobs.schedule(std::move(build_probe));
    if (jobs.wait(probe_job) != vice::jobs::Status::succeeded) {
        log(LogLevel::error, "VICE Lab could not prepare the RHI resource probe.");
        return 1;
    }

    vice::render::BufferDesc upload_desc;
    upload_desc.size = probe_source.size();
    upload_desc.usage = vice::render::BufferUsage::copy_source;
    upload_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    upload_desc.debug_name = "VICE Lab upload probe";

    vice::render::BufferDesc readback_desc;
    readback_desc.size = probe_source.size();
    readback_desc.usage = vice::render::BufferUsage::copy_destination;
    readback_desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    readback_desc.debug_name = "VICE Lab readback probe";

    auto upload_buffer = render_device->create_buffer(upload_desc);
    auto readback_buffer = render_device->create_buffer(readback_desc);
    auto transfer_commands = render_device->create_command_list();
    if (!upload_buffer || !readback_buffer || !transfer_commands) {
        log(LogLevel::error, "VICE Lab could not create RHI probe resources.");
        return 1;
    }
    if (!upload_buffer->write(0, std::span<const std::byte>{probe_source})) {
        log(LogLevel::error, "VICE Lab could not write the RHI upload buffer.");
        return 1;
    }
    if (!transfer_commands->copy_buffer(
            *upload_buffer,
            0,
            *readback_buffer,
            0,
            probe_source.size()
        )) {
        log(LogLevel::error, "VICE Lab could not record the RHI buffer copy.");
        return 1;
    }

    const auto probe_submission = render_device->submit(std::move(transfer_commands));
    if (probe_submission == vice::render::invalid_submission ||
        !render_device->wait_for_submission(probe_submission)) {
        log(LogLevel::error, "VICE Lab RHI probe submission failed.");
        return 1;
    }

    std::array<std::byte, 64> probe_result{};
    if (!readback_buffer->read(0, std::span<std::byte>{probe_result}) ||
        !std::equal(probe_source.begin(), probe_source.end(), probe_result.begin())) {
        log(LogLevel::error, "VICE Lab RHI buffer transfer verification failed.");
        return 1;
    }
    log(LogLevel::debug, "VICE Lab RHI buffer transfer verified.");

    auto swapchain = render_device->create_swapchain(*window);
    if (!swapchain) {
        log(LogLevel::error, "VICE Lab could not create a swapchain.");
        return 1;
    }

    vice::render::BufferDesc vertex_buffer_desc;
    vertex_buffer_desc.size = sizeof(triangle_vertices);
    vertex_buffer_desc.usage = vice::render::BufferUsage::vertex;
    vertex_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    vertex_buffer_desc.debug_name = "VICE Lab triangle vertices";

    vice::render::BufferDesc index_buffer_desc;
    index_buffer_desc.size = sizeof(triangle_indices);
    index_buffer_desc.usage = vice::render::BufferUsage::index;
    index_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    index_buffer_desc.debug_name = "VICE Lab triangle indices";

    auto vertex_buffer = render_device->create_buffer(vertex_buffer_desc);
    auto index_buffer = render_device->create_buffer(index_buffer_desc);
    if (!vertex_buffer || !index_buffer ||
        !vertex_buffer->write(0, std::as_bytes(std::span{triangle_vertices})) ||
        !index_buffer->write(0, std::as_bytes(std::span{triangle_indices}))) {
        log(LogLevel::error, "VICE Lab could not upload triangle mesh buffers.");
        return 1;
    }

    const bool metal_backend = render_device->backend() == vice::render::Backend::metal;
    vice::render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = vice::render::ShaderStage::vertex;
    vertex_desc.language = metal_backend
        ? vice::render::ShaderSourceLanguage::metal
        : vice::render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal_backend ? metal_triangle_source : hlsl_triangle_source;
    vertex_desc.entry_point = "vice_triangle_vs";
    vertex_desc.debug_name = "VICE Lab triangle vertex";

    vice::render::ShaderModuleDesc fragment_desc = vertex_desc;
    fragment_desc.stage = vice::render::ShaderStage::fragment;
    fragment_desc.entry_point = "vice_triangle_fs";
    fragment_desc.debug_name = "VICE Lab triangle fragment";

    const std::filesystem::path cooked_shader_root{VICE_LAB_COOKED_SHADER_ROOT};
    auto vertex_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_triangle_vs",
        vertex_desc
    );
    auto fragment_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_triangle_fs",
        fragment_desc
    );
    if (!vertex_shader || !fragment_shader) {
        log(LogLevel::error, "VICE Lab could not create triangle shader modules.");
        return 1;
    }

    vice::render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.get();
    pipeline_desc.fragment_shader = fragment_shader.get();
    pipeline_desc.color_format = swapchain->color_format();
    pipeline_desc.cull_mode = vice::render::CullMode::none;
    pipeline_desc.vertex_buffers = triangle_vertex_layouts;
    pipeline_desc.debug_name = "VICE Lab indexed triangle";

    auto triangle_pipeline = render_device->create_graphics_pipeline(pipeline_desc);
    if (!triangle_pipeline) {
        log(LogLevel::error, "VICE Lab could not create the indexed triangle pipeline.");
        return 1;
    }
    log(LogLevel::info, "VICE Lab indexed mesh pipeline ready.");

    vice::world::World world;
    const auto entity = world.create_entity();
    if (const auto persistent_id = world.persistent_id(entity)) {
        log(LogLevel::info, std::string{"World entity: "} + persistent_id->to_string());
    }

    constexpr vice::render::Color clear_color{0.018F, 0.022F, 0.030F, 1.0F};
    FrameClock clock;
    bool running = true;
    while (running) {
        vice::platform::Event event{vice::platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            std::visit(
                [&](const auto& payload) {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (
                        std::is_same_v<Payload, vice::platform::QuitRequested> ||
                        std::is_same_v<Payload, vice::platform::WindowCloseRequested>
                    ) {
                        running = false;
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowMetricsChanged>) {
                        (void)swapchain->resize(payload.metrics);
                    } else if constexpr (std::is_same_v<Payload, vice::platform::KeyEvent>) {
                        if (payload.pressed && payload.key == vice::platform::Key::escape) {
                            running = false;
                        }
                    }
                },
                event.payload
            );
        }

        (void)jobs.pump_main(64);

        if (running) {
            auto frame_commands = render_device->create_command_list();
            vice::render::RenderPassDesc pass_desc;
            pass_desc.clear_color = clear_color;

            const bool recorded = frame_commands &&
                frame_commands->begin_render_pass(*swapchain, pass_desc) &&
                frame_commands->set_graphics_pipeline(*triangle_pipeline) &&
                frame_commands->set_vertex_buffer(0, *vertex_buffer) &&
                frame_commands->set_index_buffer(
                    *index_buffer,
                    vice::render::IndexFormat::uint16
                ) &&
                frame_commands->draw_indexed(static_cast<std::uint32_t>(triangle_indices.size())) &&
                frame_commands->end_render_pass() &&
                frame_commands->present(*swapchain);

            if (!recorded) {
                log(LogLevel::error, "VICE Lab failed to record the indexed triangle frame.");
                running = false;
            } else {
                const auto frame_submission = render_device->submit(std::move(frame_commands));
                if (frame_submission == vice::render::invalid_submission) {
                    log(LogLevel::error, "VICE Lab indexed triangle submission failed.");
                    running = false;
                }
            }
        }

        clock.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    (void)render_device->wait_idle();
    triangle_pipeline.reset();
    fragment_shader.reset();
    vertex_shader.reset();
    index_buffer.reset();
    vertex_buffer.reset();
    swapchain.reset();
    readback_buffer.reset();
    upload_buffer.reset();
    render_device.reset();
    window.reset();
    jobs.shutdown();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}
