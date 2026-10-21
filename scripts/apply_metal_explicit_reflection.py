from pathlib import Path

path = Path("tools/shader_cooker/src/shader_cooker.cpp")
text = path.read_text()

old_cache = """                const MetalBindingReflection metal = explicit_layout
                    ? build_explicit_metal_binding(result.reflection)
                    : parse_metal_shader_converter_reflection(
                        read_file(*result.metal_reflection_path), result.reflection
                    );
"""
new_cache = """                const MetalBindingReflection metal = explicit_layout
                    ? parse_explicit_metal_shader_converter_reflection(
                        read_file(*result.metal_reflection_path), result.reflection
                    )
                    : parse_metal_shader_converter_reflection(
                        read_file(*result.metal_reflection_path), result.reflection
                    );
"""
old_fresh = """        metal_binding = explicit_layout
            ? build_explicit_metal_binding(result.reflection)
            : parse_metal_shader_converter_reflection(
                read_file(*result.metal_reflection_path), result.reflection
            );
"""
new_fresh = """        metal_binding = explicit_layout
            ? parse_explicit_metal_shader_converter_reflection(
                read_file(*result.metal_reflection_path), result.reflection
            )
            : parse_metal_shader_converter_reflection(
                read_file(*result.metal_reflection_path), result.reflection
            );
"""

for old, new, name in (
    (old_cache, new_cache, "cache-hit explicit Metal binding"),
    (old_fresh, new_fresh, "fresh explicit Metal binding"),
):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Expected exactly one {name} block, found {count}")
    text = text.replace(old, new)

if "build_explicit_metal_binding(" in text:
    raise SystemExit("Legacy manual explicit Metal binding call remains in shader cooker")

path.write_text(text)
