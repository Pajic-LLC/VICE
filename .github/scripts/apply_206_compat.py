from pathlib import Path

path = Path('tools/shader_cooker/src/shader_cooker.cpp')
text = path.read_text()

if 'const bool explicit_metal_binding =' not in text:
    anchor = '''    std::ostringstream manifest;\n    manifest\n        << "{\\n"\n        << "  \\\"format\\\": \\\"vice.shader\\\",\\n"\n        << "  \\\"version\\\": 5,\\n"'''
    replacement = '''    const bool explicit_metal_binding = metal_binding.has_value() &&\n        metal_binding->model == MetalBindingModel::explicit_descriptor_tables;\n    const std::uint32_t manifest_version = explicit_metal_binding ? 5U : 4U;\n    const std::uint32_t metal_binding_version = explicit_metal_binding ? 3U : 2U;\n\n    std::ostringstream manifest;\n    manifest\n        << "{\\n"\n        << "  \\\"format\\\": \\\"vice.shader\\\",\\n"\n        << "  \\\"version\\\": " << manifest_version << ",\\n"'''
    if anchor not in text:
        raise SystemExit('missing build_manifest version anchor')
    text = text.replace(anchor, replacement, 1)

old = '''        << "  },\\n"\n        << "  \\\"metal_binding\\\": {\\n"\n        << "    \\\"version\\\": 3,\\n"\n        << "    \\\"model\\\": ";'''
new = '''        << "  },\\n"\n        << "  \\\"metal_binding\\\": {\\n"\n        << "    \\\"version\\\": " << metal_binding_version << ",\\n"\n        << "    \\\"model\\\": ";'''
if new not in text:
    if old not in text:
        raise SystemExit('missing metal binding version anchor')
    text = text.replace(old, new, 1)

path.write_text(text)
