from pathlib import Path

path = Path("tools/mesh_cooker/src/mesh_cooker.cpp")
text = path.read_text()
old = '''    if (source.name.empty() || source.vertices.size() < 3U || source.indices.size() < 3U ||
        source.indices.size() % 3U != 0U || !render::valid(source.stable_id)) {
'''
new = '''    if (source.name.empty() || source.vertices.size() < 3U || source.indices.size() < 3U ||
        source.indices.size() % 3U != 0U) {
'''
if text.count(old) != 1:
    raise RuntimeError(f"expected one legacy cook guard, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
