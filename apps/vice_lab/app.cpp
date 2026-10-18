#include <vice/core/assert.hpp>
#include <vice/core/log.hpp>
#include <vice/core/time.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/math/math.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/jolt_backend.hpp>
#include <vice/rigid_body/material_asset.hpp>
#include <vice/rigid_body/material_library.hpp>
#include <vice/rigid_body/rigid_body.hpp>
#include <vice/rigid_body/world_bridge.hpp>
#include <vice/simulation/domain.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/simulation/simulation.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include "lab_materials.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

namespace {

using LabVertex = vice::render::MeshVertex;

struct LabInputState {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump_queued = false;
};

static_assert(sizeof(LabVertex) == 44U);
static_assert(sizeof(vice::math::Mat4) == 64U);

constexpr std::string_view player_persistent_id_text =
    "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_persistent_id_text =
    "10000000-0000-4000-8000-000000000002";

constexpr vice::world::RegionId region_a{1};
constexpr vice::world::RegionId region_b{2};
constexpr vice::simulation::DomainId rigid_domain_a_id{100};
constexpr vice::simulation::DomainId rigid_domain_b_id{200};
constexpr vice::simulation::ParticipantId terrain_participant{1};
constexpr vice::simulation::ParticipantId cube_participant{2};
constexpr vice::simulation::ParticipantId ramp_participant{3};

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

static constexpr std::array<vice::render::VertexAttributeDesc, 2> cube_attributes{
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

static constexpr std::array<vice::render::VertexBufferLayoutDesc, 1> cube_vertex_layouts{
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(LabVertex),
        .step_mode = vice::render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = cube_attributes,
    },
};

struct CubeInstanceData {
    vice::math::Mat4 model;
};

static constexpr std::array<vice::render::VertexAttributeDesc, 4> cube_instance_attributes{
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

static constexpr std::array<vice::render::VertexBufferLayoutDesc, 1> cube_instance_layouts{
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(CubeInstanceData),
        .step_mode = vice::render::VertexStepMode::per_instance,
        .step_rate = 1,
        .attributes = cube_instance_attributes,
    },
};

// Remaining VICE Lab implementation is unchanged below this point.
