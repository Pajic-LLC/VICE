from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

path = Path("apps/vice_editor/src/asset_index.cpp")
text = path.read_text()
text = once(text,
    "#include <vice/render/shader_asset.hpp>\n",
    "#include <vice/render/shader_asset.hpp>\n#include <vice/render/texture_asset.hpp>\n",
    "texture include")
text = once(text,
    "    if (extension == \".vmat\") return AssetKind::render_material;\n",
    "    if (extension == \".vmat\") return AssetKind::render_material;\n    if (extension == \".vtex\") return AssetKind::texture;\n",
    "texture source classification")
text = once(text,
    "    if (extension == \".vmatc\") return AssetKind::render_material;\n",
    "    if (extension == \".vmatc\") return AssetKind::render_material;\n    if (extension == \".vtexc\") return AssetKind::texture;\n",
    "texture cooked classification")
text = once(text,
    "    case AssetKind::render_material: relative.replace_extension(\".vmatc\"); break;\n",
    "    case AssetKind::render_material: relative.replace_extension(\".vmatc\"); break;\n    case AssetKind::texture: relative.replace_extension(\".vtexc\"); break;\n",
    "texture cooked path")
material_case = '''    case AssetKind::render_material: {
        const auto loaded = render::load_render_material_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = "Base color + project vertex/fragment shaders";
            if (render::valid(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
'''
new_material_case = '''    case AssetKind::render_material: {
        const auto loaded = render::load_render_material_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = loaded.asset->version >= render::render_material_asset_format_version_v2
                ? "Textured PBR surface"
                : "Base color + project vertex/fragment shaders";
            if (render::valid_render_material_id(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
    case AssetKind::texture: {
        const auto loaded = render::load_texture_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = std::to_string(loaded.asset->width) + " x " +
                std::to_string(loaded.asset->height) +
                (loaded.asset->color_space == render::TextureColorSpace::srgb ? " sRGB" : " linear");
            if (render::valid_texture_id(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
'''
text = once(text, material_case, new_material_case, "texture cooked inspection")
text = once(text,
    "    case AssetKind::render_material: return \"Render Material\";\n",
    "    case AssetKind::render_material: return \"Render Material\";\n    case AssetKind::texture: return \"Texture\";\n",
    "texture kind name")
path.write_text(text)

ui_path = Path("apps/vice_editor/src/resource_browser_ui.cpp")
ui = ui_path.read_text()
ui = once(ui,
    "    case world::ResourceRole::render_mesh: return \"Render mesh\";\n",
    "    case world::ResourceRole::render_mesh: return \"Render mesh\";\n    case world::ResourceRole::render_material: return \"Render material\";\n",
    "render material role label")
old = '''    static constexpr std::array<const char*, 7> kind_names{
        "All",
        "World",
        "Mesh",
        "Collision",
        "Rigid Material",
        "Shader",
        "Shader Manifest",
    };
    ImGui::SetNextItemWidth(150.0F);
    const char* preview = state_->kind_filter < 0
        ? kind_names[0]
        : kind_names[static_cast<std::size_t>(state_->kind_filter + 1)];
    if (ImGui::BeginCombo("##ResourceKind", preview)) {
        if (ImGui::Selectable("All", state_->kind_filter < 0)) state_->kind_filter = -1;
        for (int index = 0; index < 6; ++index) {
            if (ImGui::Selectable(kind_names[static_cast<std::size_t>(index + 1)], state_->kind_filter == index)) {
                state_->kind_filter = index;
            }
        }
        ImGui::EndCombo();
    }
'''
new = '''    struct KindChoice {
        const char* name;
        AssetKind kind;
    };
    static constexpr std::array kind_choices{
        KindChoice{"World", AssetKind::world},
        KindChoice{"Mesh", AssetKind::mesh},
        KindChoice{"Collision", AssetKind::collision},
        KindChoice{"Rigid Material", AssetKind::rigid_material},
        KindChoice{"Shader", AssetKind::shader_source},
        KindChoice{"Shader Manifest", AssetKind::shader_manifest},
        KindChoice{"Render Material", AssetKind::render_material},
        KindChoice{"Texture", AssetKind::texture},
    };
    ImGui::SetNextItemWidth(150.0F);
    const char* preview = "All";
    if (state_->kind_filter >= 0) {
        for (const auto& choice : kind_choices) {
            if (static_cast<int>(choice.kind) == state_->kind_filter) {
                preview = choice.name;
                break;
            }
        }
    }
    if (ImGui::BeginCombo("##ResourceKind", preview)) {
        if (ImGui::Selectable("All", state_->kind_filter < 0)) state_->kind_filter = -1;
        for (const auto& choice : kind_choices) {
            const int value = static_cast<int>(choice.kind);
            if (ImGui::Selectable(choice.name, state_->kind_filter == value)) {
                state_->kind_filter = value;
            }
        }
        ImGui::EndCombo();
    }
'''
ui = once(ui, old, new, "asset kind filter")
ui_path.write_text(ui)
