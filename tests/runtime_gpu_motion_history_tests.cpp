#include "gpu_motion_history.hpp"

#include <cassert>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    std::vector<RenderMeshInstance> instances(3U);
    instances[0].entity = required_id("91000000-0000-4000-8000-000000000001");
    instances[1].entity = required_id("91000000-0000-4000-8000-000000000002");
    instances[2].entity = required_id("91000000-0000-4000-8000-000000000003");

    const std::vector<math::Mat4> current_models{
        math::translation({1.0F, 0.0F, 0.0F}),
        math::translation({2.0F, 0.0F, 0.0F}),
        math::translation({3.0F, 0.0F, 0.0F}),
    };
    detail::RuntimePreviousModelMap previous_models;
    previous_models.emplace(
        instances[0].entity,
        math::translation({10.0F, 0.0F, 0.0F})
    );
    previous_models.emplace(
        instances[2].entity,
        math::translation({30.0F, 0.0F, 0.0F})
    );

    const auto with_history = detail::build_runtime_gpu_previous_models(
        instances,
        current_models,
        previous_models,
        true
    );
    assert(with_history);
    assert(with_history.models.size() == instances.size());
    assert(with_history.models[0].values == math::translation({10.0F, 0.0F, 0.0F}).values);
    assert(with_history.models[1].values == current_models[1].values);
    assert(with_history.models[2].values == math::translation({30.0F, 0.0F, 0.0F}).values);

    const auto without_history = detail::build_runtime_gpu_previous_models(
        instances,
        current_models,
        previous_models,
        false
    );
    assert(without_history);
    assert(without_history.models.size() == current_models.size());
    for (std::size_t index = 0U; index < current_models.size(); ++index) {
        assert(without_history.models[index].values == current_models[index].values);
    }

    const auto mismatched = detail::build_runtime_gpu_previous_models(
        instances,
        std::span<const math::Mat4>{current_models}.first(2U),
        previous_models,
        true
    );
    assert(!mismatched);
    assert(!mismatched.diagnostics.empty());

    return 0;
}
