from pathlib import Path

path = Path("tools/shader_cooker/src/shader_cooker.cpp")
text = path.read_text()
old = 'hash_append(material, "VICE_SHADER_ASSET_V5_METAL_EXPLICIT_TABLES1");'
new = 'hash_append(material, "VICE_SHADER_ASSET_V5_METAL_EXPLICIT_TABLES2");'
count = text.count(old)
if count != 1:
    raise SystemExit(f"Expected one explicit Metal cache salt, found {count}")
path.write_text(text.replace(old, new))
