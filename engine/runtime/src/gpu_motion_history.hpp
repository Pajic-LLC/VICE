#pragma once

#include <vice/runtime/scene.hpp>

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace vice::runtime::detail {

using RuntimePreviousModelMap = std::unordered_map<
    world::PersistentId,
    math::Mat4,
    world::PersistentIdHash
>;

struct RuntimeGpuPreviousModelBuildResult {
    std::vector<math::Mat4> models;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] inline RuntimeGpuPreviousModelBuildResult build_runtime_gpu_previous_models(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const math::Mat4> current_models,
    const RuntimePreviousModelMap& previous_models,
    const bool history_valid
) {
    RuntimeGpuPreviousModelBuildResult result;
    if (instances.size() != current_models.size()) {
        result.diagnostics =
            "Runtime GPU motion history requires one current model per scene instance.";
        return result;
    }

    result.models.reserve(instances.size());
    for (std::size_t index = 0U; index < instances.size(); ++index) {
        math::Mat4 previous_model = current_models[index];
        if (history_valid) {
            const auto previous = previous_models.find(instances[index].entity);
            if (previous != previous_models.end()) previous_model = previous->second;
        }
        result.models.push_back(previous_model);
    }
    return result;
}

} // namespace vice::runtime::detail
