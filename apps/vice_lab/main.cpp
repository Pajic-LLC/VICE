#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/math/math.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/entity_bindings.hpp>
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
    float position[3];
    float color[3];
};

struct LabInputState {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump_queued = false;
};

static_assert(sizeof(LabVertex) == 24U);
static_assert(sizeof(vice::math::Mat4) == 64U);

static constexpr std::array<LabVertex, 8> cube_vertices{{
    {{-0.7F, -0.7F, -0.7F}, {0.16F, 0.68F, 1.00F}},
    {{ 0.7F, -0.7F, -0.7F}, {0.96F, 0.26F, 0.54F}},
    {{ 0.7F,  0.7F, -0.7F}, {0.98F, 0.80F, 0.18F}},
    {{-0.7F,  0.7F, -0.7F}, {0.34F, 0.92F, 0.48F}},
    {{-0.7F, -0.7F,  0.7F}, {0.66F, 0.34F, 1.00F}},
    {{ 0.7F, -0.7F,  0.7F}, {1.00F, 0.48F, 0.20F}},
    {{ 0.7F,  0.7F,  0.7F}, {0.24F, 0.88F, 0.92F}},
    {{-0.7F,  0.7F,  0.7F}, {0.96F, 0.42F, 0.82F}},
}};

static constexpr std::array<std::uint16_t, 36> cube_indices{
    4, 5, 6, 4, 6, 7,
    1, 0, 3, 1, 3, 2,
    0, 4, 7, 0, 7, 3,
    5, 1, 2, 5, 2, 6,
    3, 7, 6, 3, 6, 2,
    0, 1, 5, 0, 5, 4,
};

static constexpr std::array cube_vertex_attributes{
    vice::render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = vice::render::VertexFormat::float32x3,
        .offset = 0,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = vice::render::VertexFormat::float32x3,
        .offset = 12,
    },
};

static constexpr std::array cube_instance_attributes{
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0,
        .shader_location = 2,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 0,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 1,
        .shader_location = 3,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 16,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 2,
        .shader_location = 4,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 32,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 3,
        .shader_location = 5,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 48,
    },
};

static constexpr std::array cube_vertex_layouts{
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(LabVertex),
        .step_mode = vice::render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = cube_vertex_attributes,
    },
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(vice::math::Mat4),
        .step_mode = vice::render::VertexStepMode::per_instance,
        .step_rate = 1,
        .attributes = cube_instance_attributes,
    },
};

constexpr std::string_view metal_cube_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
    float4 model_col0 [[attribute(13)]];
    float4 model_col1 [[attribute(14)]];
    float4 model_col2 [[attribute(15)]];
    float4 model_col3 [[attribute(16)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vice_cube_vs(VertexIn input [[stage_in]]) {
    float4 world_position =
        input.model_col0 * input.position.x +
        input.model_col1 * input.position.y +
        input.model_col2 * input.position.z +
        input.model_col3;
    float z_view = world_position.z - 5.0;

    VertexOut out;
    out.position = float4(
        0.9742786 * world_position.x,
        1.7320508 * world_position.y,
        -1.001001 * z_view - 0.1001001,
        -z_view
    );
    out.color = input.color;
    return out;
}

fragment float4 vice_cube_fs(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
)";

constexpr std::string_view hlsl_cube_source = R"(
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float4 model_col0 : TEXCOORD0;
    float4 model_col1 : TEXCOORD1;
    float4 model_col2 : TEXCOORD2;
    float4 model_col3 : TEXCOORD3;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_cube_vs(VertexIn input) {
    float4 world_position =
        input.model_col0 * input.position.x +
        input.model_col1 * input.position.y +
        input.model_col2 * input.position.z +
        input.model_col3;
    float z_view = world_position.z - 5.0;

    VertexOut output;
    output.position = float4(
        0.9742786 * world_position.x,
        1.7320508 * world_position.y,
        -1.001001 * z_view - 0.1001001,
        -z_view
    );
    output.color = input.color;
    return output;
}

float4 vice_cube_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] std::string_view character_ground_state_name(
    const vice::rigid_body::CharacterGroundState state
) noexcept {
    switch (state) {
        case vice::rigid_body::CharacterGroundState::grounded:
            return "grounded";
        case vice::rigid_body::CharacterGroundState::steep_ground:
            return "steep_ground";
        case vice::rigid_body::CharacterGroundState::unsupported:
            return "unsupported";
        case vice::rigid_body::CharacterGroundState::airborne:
            return "airborne";
    }
    return "unknown";
}

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

[[nodiscard]] std::unique_ptr<vice::render::Texture> create_depth_target(
    vice::render::Device& device,
    const vice::platform::WindowMetrics metrics
) {
    vice::render::TextureDesc depth_desc;
    depth_desc.dimension = vice::render::TextureDimension::d2;
    depth_desc.width = std::max<std::uint32_t>(1U, metrics.drawable_width);
    depth_desc.height = std::max<std::uint32_t>(1U, metrics.drawable_height);
    depth_desc.depth = 1;
    depth_desc.mip_levels = 1;
    depth_desc.array_layers = 1;
    depth_desc.sample_count = 1;
    depth_desc.format = vice::render::TextureFormat::depth32_float;
    depth_desc.usage = vice::render::TextureUsage::depth_attachment;
    depth_desc.debug_name = "VICE Lab depth";
    return device.create_texture(depth_desc);
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

    auto depth_target = create_depth_target(*render_device, window->metrics());
    if (!depth_target) {
        log(LogLevel::error, "VICE Lab could not create the depth target.");
        return 1;
    }

    vice::render::BufferDesc vertex_buffer_desc;
    vertex_buffer_desc.size = sizeof(cube_vertices);
    vertex_buffer_desc.usage = vice::render::BufferUsage::vertex;
    vertex_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    vertex_buffer_desc.debug_name = "VICE Lab cube vertices";

    vice::render::BufferDesc instance_buffer_desc;
    instance_buffer_desc.size = sizeof(vice::math::Mat4);
    instance_buffer_desc.usage = vice::render::BufferUsage::vertex;
    instance_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    instance_buffer_desc.debug_name = "VICE Lab rigid pose";

    vice::render::BufferDesc index_buffer_desc;
    index_buffer_desc.size = sizeof(cube_indices);
    index_buffer_desc.usage = vice::render::BufferUsage::index;
    index_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    index_buffer_desc.debug_name = "VICE Lab cube indices";

    vice::render::BufferDesc frame_buffer_desc;
    frame_buffer_desc.size = sizeof(vice::math::Mat4);
    frame_buffer_desc.usage = vice::render::BufferUsage::uniform;
    frame_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    frame_buffer_desc.debug_name = "VICE Lab frame constants";

    auto vertex_buffer = render_device->create_buffer(vertex_buffer_desc);
    auto instance_buffer = render_device->create_buffer(instance_buffer_desc);
    auto index_buffer = render_device->create_buffer(index_buffer_desc);
    auto frame_buffer = render_device->create_buffer(frame_buffer_desc);
    if (!vertex_buffer || !instance_buffer || !index_buffer || !frame_buffer ||
        !vertex_buffer->write(0, std::as_bytes(std::span{cube_vertices})) ||
        !index_buffer->write(0, std::as_bytes(std::span{cube_indices}))) {
        log(LogLevel::error, "VICE Lab could not upload cube resources.");
        return 1;
    }

    const bool metal_backend = render_device->backend() == vice::render::Backend::metal;
    vice::render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = vice::render::ShaderStage::vertex;
    vertex_desc.language = metal_backend
        ? vice::render::ShaderSourceLanguage::metal
        : vice::render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal_backend ? metal_cube_source : hlsl_cube_source;
    vertex_desc.entry_point = "vice_cube_vs";
    vertex_desc.debug_name = "VICE Lab cube vertex";

    vice::render::ShaderModuleDesc fragment_desc = vertex_desc;
    fragment_desc.stage = vice::render::ShaderStage::fragment;
    fragment_desc.entry_point = "vice_cube_fs";
    fragment_desc.debug_name = "VICE Lab cube fragment";

    const std::filesystem::path cooked_shader_root{VICE_LAB_COOKED_SHADER_ROOT};
    auto vertex_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_cube_vs",
        vertex_desc
    );
    auto fragment_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_cube_fs",
        fragment_desc
    );
    if (!vertex_shader || !fragment_shader) {
        log(LogLevel::error, "VICE Lab could not create cube shader modules.");
        return 1;
    }

    vice::render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.get();
    pipeline_desc.fragment_shader = fragment_shader.get();
    pipeline_desc.color_format = swapchain->color_format();
    pipeline_desc.cull_mode = vice::render::CullMode::none;
    pipeline_desc.depth.format = vice::render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = vice::render::CompareOp::less;
    pipeline_desc.vertex_buffers = cube_vertex_layouts;
    pipeline_desc.debug_name = "VICE Lab simulated cube";

    auto cube_pipeline = render_device->create_graphics_pipeline(pipeline_desc);
    if (!cube_pipeline) {
        log(LogLevel::error, "VICE Lab could not create the cube pipeline.");
        return 1;
    }
    log(LogLevel::info, "VICE Lab simulated cube pipeline ready.");

    const bool cooked_transform_binding = !vertex_shader->resources().empty();
    constexpr vice::render::ShaderBindingKey frame_binding{
        .register_class = vice::render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .array_element = 0,
    };

    vice::world::World world;
    const auto entity = world.create_entity();
    if (const auto persistent_id = world.persistent_id(entity)) {
        log(LogLevel::info, std::string{"World entity: "} + persistent_id->to_string());
    }

    constexpr vice::simulation::DomainId rigid_domain_id{100};
    constexpr vice::simulation::ParticipantId floor_participant{1};
    constexpr vice::simulation::ParticipantId cube_participant{2};
    constexpr vice::rigid_body::CharacterId player_character{1};

    auto rigid_domain = vice::rigid_body::Domain::create(rigid_domain_id, "lab.rigid", jobs);
    vice::simulation::Coordinator simulation{1.0 / 120.0};
    vice::simulation::EntityBindings bindings;
    if (!rigid_domain || !simulation.register_domain(*rigid_domain)) {
        log(LogLevel::error, "VICE Lab could not initialize rigid simulation.");
        return 1;
    }

    vice::rigid_body::BoxBodyDesc floor;
    floor.participant = floor_participant;
    floor.motion = vice::rigid_body::MotionType::static_body;
    floor.half_extents = vice::math::Vec3{8.0F, 0.25F, 8.0F};
    floor.position = vice::math::Vec3{0.0F, -0.25F, 0.0F};

    vice::rigid_body::BoxBodyDesc simulated_cube;
    simulated_cube.participant = cube_participant;
    simulated_cube.motion = vice::rigid_body::MotionType::dynamic_body;
    simulated_cube.half_extents = vice::math::Vec3{0.7F, 0.7F, 0.7F};
    simulated_cube.position = vice::math::Vec3{0.0F, 2.2F, 0.0F};
    simulated_cube.orientation = vice::math::Quat{0.0F, 0.0F, 0.2588190451F, 0.9659258263F};
    simulated_cube.friction = 0.65F;
    simulated_cube.restitution = 0.15F;

    vice::rigid_body::CharacterDesc player;
    player.id = player_character;
    player.position = vice::math::Vec3{0.0F, 0.0F, 5.5F};
    player.height = 1.8F;
    player.radius = 0.3F;
    player.step_height = 0.4F;
    player.stick_to_floor_distance = 0.5F;

    if (!rigid_domain->create_box(floor) ||
        !rigid_domain->create_box(simulated_cube) ||
        !rigid_domain->create_character(player) ||
        !bindings.bind(world, simulation, entity, rigid_domain_id, cube_participant)) {
        log(LogLevel::error, "VICE Lab could not create the playable simulated world.");
        return 1;
    }
    log(LogLevel::info, "VICE Lab world entity is bound to a Jolt-backed rigid participant.");
    log(LogLevel::info, "VICE Lab controls: W/A/S/D or arrows move, Space jumps, Escape quits.");

    constexpr float player_speed = 3.5F;
    constexpr float player_jump_speed = 5.5F;
    constexpr vice::render::Color clear_color{0.018F, 0.022F, 0.030F, 1.0F};
    FrameClock clock;
    LabInputState input;
    auto previous_ground_state = vice::rigid_body::CharacterGroundState::airborne;
    bool ground_state_initialized = false;
    double simulation_accumulator = 0.0;
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
                        if (payload.metrics.drawable_width > 0U && payload.metrics.drawable_height > 0U) {
                            if (!render_device->wait_idle() || !swapchain->resize(payload.metrics)) {
                                log(LogLevel::error, "VICE Lab could not resize the render targets.");
                                running = false;
                                return;
                            }
                            auto replacement_depth = create_depth_target(*render_device, payload.metrics);
                            if (!replacement_depth) {
                                log(LogLevel::error, "VICE Lab could not resize the depth target.");
                                running = false;
                                return;
                            }
                            depth_target = std::move(replacement_depth);
                        }
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowFocusChanged>) {
                        if (!payload.focused) {
                            input = {};
                        }
                    } else if constexpr (std::is_same_v<Payload, vice::platform::KeyEvent>) {
                        if (payload.pressed && payload.key == vice::platform::Key::escape) {
                            running = false;
                            return;
                        }

                        switch (payload.key) {
                            case vice::platform::Key::w:
                            case vice::platform::Key::up:
                                input.forward = payload.pressed;
                                break;
                            case vice::platform::Key::s:
                            case vice::platform::Key::down:
                                input.backward = payload.pressed;
                                break;
                            case vice::platform::Key::a:
                            case vice::platform::Key::left:
                                input.left = payload.pressed;
                                break;
                            case vice::platform::Key::d:
                            case vice::platform::Key::right:
                                input.right = payload.pressed;
                                break;
                            case vice::platform::Key::space:
                                if (payload.pressed && !payload.repeat) {
                                    input.jump_queued = true;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                },
                event.payload
            );
        }

        (void)jobs.pump_main(64);
        clock.tick();
        simulation_accumulator += std::min(clock.delta_seconds(), 0.25);
        while (running && simulation_accumulator >= simulation.fixed_delta_seconds()) {
            float move_x = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
            float move_z = (input.backward ? 1.0F : 0.0F) - (input.forward ? 1.0F : 0.0F);
            if (move_x != 0.0F && move_z != 0.0F) {
                constexpr float diagonal_scale = 0.7071067812F;
                move_x *= diagonal_scale;
                move_z *= diagonal_scale;
            }

            if (!rigid_domain->set_character_horizontal_velocity(
                    player_character,
                    vice::math::Vec3{move_x * player_speed, 0.0F, move_z * player_speed}
                )) {
                log(LogLevel::error, "VICE Lab could not update player movement.");
                running = false;
                break;
            }

            if (input.jump_queued) {
                if (!rigid_domain->request_character_jump(player_character, player_jump_speed)) {
                    log(LogLevel::error, "VICE Lab could not queue the player jump.");
                    running = false;
                    break;
                }
                input.jump_queued = false;
            }

            simulation.step();
            simulation_accumulator -= simulation.fixed_delta_seconds();
        }

        if (running) {
            const auto body_state = rigid_domain->body_state(cube_participant);
            const auto player_state = rigid_domain->character_state(player_character);
            if (!body_state || !player_state) {
                log(LogLevel::error, "VICE Lab lost a live simulated participant.");
                running = false;
                continue;
            }

            if (!ground_state_initialized || player_state->ground_state != previous_ground_state) {
                previous_ground_state = player_state->ground_state;
                ground_state_initialized = true;
                log(
                    LogLevel::debug,
                    std::string{"VICE Lab player ground state: "} +
                        std::string{character_ground_state_name(player_state->ground_state)}
                );
            }

            auto render_position = body_state->position;
            if (!cooked_transform_binding) {
                render_position.x -= player_state->position.x;
                render_position.y -= player_state->position.y;
                render_position.z -= player_state->position.z;
            }
            const auto model = vice::math::pose(render_position, body_state->orientation);
            if (!instance_buffer->write(0, std::as_bytes(std::span{model.values}))) {
                log(LogLevel::error, "VICE Lab could not upload the simulated cube pose.");
                running = false;
                continue;
            }

            const float aspect = static_cast<float>(depth_target->width()) /
                static_cast<float>(std::max<std::uint32_t>(1U, depth_target->height()));
            const auto camera_target = vice::math::Vec3{
                player_state->position.x,
                player_state->position.y + 1.0F,
                player_state->position.z,
            };
            const auto camera_position = vice::math::Vec3{
                player_state->position.x,
                player_state->position.y + 3.2F,
                player_state->position.z + 6.5F,
            };
            const auto view = vice::math::look_at_rh(
                camera_position,
                camera_target,
                vice::math::Vec3{0.0F, 1.0F, 0.0F}
            );
            const auto projection = vice::math::perspective_rh_zo(
                1.0471975512F,
                aspect,
                0.1F,
                100.0F
            );
            const auto view_projection = vice::math::multiply(projection, view);
            if (!frame_buffer->write(0, std::as_bytes(std::span{view_projection.values}))) {
                log(LogLevel::error, "VICE Lab could not update frame constants.");
                running = false;
                continue;
            }

            auto frame_commands = render_device->create_command_list();
            vice::render::RenderPassDesc pass_desc;
            pass_desc.clear_color = clear_color;
            pass_desc.depth_attachment = depth_target.get();
            pass_desc.clear_depth = 1.0F;
            pass_desc.clear_depth_attachment = true;

            bool recorded = frame_commands &&
                frame_commands->begin_render_pass(*swapchain, pass_desc) &&
                frame_commands->set_graphics_pipeline(*cube_pipeline) &&
                frame_commands->set_vertex_buffer(0, *vertex_buffer) &&
                frame_commands->set_vertex_buffer(1, *instance_buffer) &&
                frame_commands->set_index_buffer(
                    *index_buffer,
                    vice::render::IndexFormat::uint16
                );
            if (recorded && cooked_transform_binding) {
                recorded = frame_commands->set_graphics_constant_buffer(
                    frame_binding,
                    *frame_buffer,
                    0,
                    sizeof(vice::math::Mat4)
                );
            }
            recorded = recorded &&
                frame_commands->draw_indexed(static_cast<std::uint32_t>(cube_indices.size())) &&
                frame_commands->end_render_pass() &&
                frame_commands->present(*swapchain);

            if (!recorded) {
                log(LogLevel::error, "VICE Lab failed to record the playable frame.");
                running = false;
            } else {
                const auto frame_submission = render_device->submit(std::move(frame_commands));
                if (frame_submission == vice::render::invalid_submission) {
                    log(LogLevel::error, "VICE Lab frame submission failed.");
                    running = false;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    (void)render_device->wait_idle();
    (void)bindings.unbind(entity, rigid_domain_id);
    (void)rigid_domain->destroy_character(player_character);
    (void)rigid_domain->destroy_body(cube_participant);
    (void)rigid_domain->destroy_body(floor_participant);
    (void)simulation.unregister_domain(rigid_domain_id);

    cube_pipeline.reset();
    fragment_shader.reset();
    vertex_shader.reset();
    frame_buffer.reset();
    index_buffer.reset();
    instance_buffer.reset();
    vertex_buffer.reset();
    depth_target.reset();
    swapchain.reset();
    readback_buffer.reset();
    upload_buffer.reset();
    render_device.reset();
    window.reset();
    jobs.shutdown();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}