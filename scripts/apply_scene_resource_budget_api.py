from pathlib import Path

path = Path('engine/runtime/include/vice/runtime/scene.hpp')
text = path.read_text()

anchor = '''inline constexpr std::size_t runtime_local_reflection_probe_count = 2U;
'''
insert = '''inline constexpr std::size_t runtime_local_reflection_probe_count = 2U;
inline constexpr std::uint64_t default_runtime_geometry_residency_budget =
    512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t default_runtime_texture_residency_budget =
    1024ULL * 1024ULL * 1024ULL;

struct RuntimeRenderResourceBudgets {
    std::uint64_t geometry_bytes = default_runtime_geometry_residency_budget;
    std::uint64_t texture_bytes = default_runtime_texture_residency_budget;

    friend bool operator==(const RuntimeRenderResourceBudgets&, const RuntimeRenderResourceBudgets&) = default;
};
'''
if anchor not in text:
    raise SystemExit('budget type anchor not found')
text = text.replace(anchor, insert, 1)

anchor = '''    [[nodiscard]] bool set_vignette(
        float intensity,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool render(
'''
insert = '''    [[nodiscard]] bool set_vignette(
        float intensity,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool set_resource_budgets(
        RuntimeRenderResourceBudgets budgets,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool render(
'''
if anchor not in text:
    raise SystemExit('budget setter anchor not found')
text = text.replace(anchor, insert, 1)

path.write_text(text)
