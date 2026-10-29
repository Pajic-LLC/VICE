#define CGLTF_IMPLEMENTATION
#include <algorithm>
#include <cgltf.h>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vice/tools/presentation_cooker.hpp>
namespace vice::tools {
namespace {
world::PersistentId derived(world::PersistentId seed, unsigned kind, std::uint32_t index) {
    auto bytes = seed.bytes();
    bytes[0] ^= static_cast<std::uint8_t>(kind);
    for (unsigned i = 0; i < 4; ++i)
        bytes[12 + i] ^= static_cast<std::uint8_t>((index >> (i * 8)) & 255);
    return world::PersistentId{bytes};
}
bool fail(std::string *d, const char *s) {
    if (d)
        *d = s;
    return false;
}
struct Budget {
    std::size_t used = 0;
    struct alignas(std::max_align_t) Header {
        std::size_t bytes;
    };
    static void *allocate(void *user, cgltf_size count) {
        auto &self = *static_cast<Budget *>(user);
        if (count > 64 * 1024 * 1024 - sizeof(Header) ||
            self.used > 64 * 1024 * 1024 - count - sizeof(Header))
            return nullptr;
        const auto total = count + sizeof(Header);
        auto *h = static_cast<Header *>(std::malloc(total));
        if (!h)
            return nullptr;
        h->bytes = total;
        self.used += total;
        return h + 1;
    }
    static void release(void *user, void *pointer) {
        if (!pointer)
            return;
        auto &self = *static_cast<Budget *>(user);
        auto *h = static_cast<Header *>(pointer) - 1;
        self.used -= h->bytes;
        std::free(h);
    }
};
struct Vertex {
    float p[3]{}, n[3]{0, 1, 0}, color[3]{1, 1, 1}, uv[2]{};
};
} // namespace
bool import_rig(const std::filesystem::path &source, const std::filesystem::path &output,
                world::ResourceId id, world::PersistentId root, std::string *d) {
    if (!id.valid() || root.is_nil())
        return fail(d, "Rig and root UUIDs must be non-nil.");
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(source, ec);
    if (ec || bytes > 16 * 1024 * 1024)
        return fail(d, "glTF source is missing or exceeds 16 MiB.");
    if (std::filesystem::exists(output, ec) || ec)
        return fail(d, "Import destination must be a new directory.");
    Budget budget;
    cgltf_options options{};
    options.memory.alloc_func = &Budget::allocate;
    options.memory.free_func = &Budget::release;
    options.memory.user_data = &budget;
    cgltf_data *parsed = nullptr;
    if (cgltf_parse_file(&options, source.string().c_str(), &parsed) != cgltf_result_success)
        return fail(d, "Could not parse glTF 2.0.");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, &cgltf_free);
    if (data->nodes_count == 0 || data->nodes_count > 128 || data->skins_count > 128 ||
        data->animations_count == 0 || data->animations_count > 64 || data->scenes_count != 1)
        return fail(d,
                    "Import supports one scene, 1-128 nodes, up to 128 skins and 1-64 animations.");
    std::size_t buffer_bytes = 0;
    for (std::size_t i = 0; i < data->buffers_count; ++i) {
        const auto &b = data->buffers[i];
        if (b.size > 16 * 1024 * 1024 || buffer_bytes > 32 * 1024 * 1024 - b.size)
            return fail(d, "glTF buffer capacity exceeded.");
        buffer_bytes += b.size;
        if (b.uri && std::string_view(b.uri).rfind("data:", 0) != 0)
            return fail(d, "First rig importer requires embedded buffers or GLB; external paths "
                           "are rejected.");
    }
    if (cgltf_load_buffers(&options, data.get(), source.string().c_str()) != cgltf_result_success ||
        cgltf_validate(data.get()) != cgltf_result_success)
        return fail(d, "Invalid glTF buffers/accessors.");
    std::vector<const cgltf_node *> nodes;
    std::unordered_map<const cgltf_node *, std::uint32_t> indices;
    std::vector<unsigned char> visited(data->nodes_count, 0);
    bool graph_ok = true;
    std::function<void(const cgltf_node *)> visit = [&](const cgltf_node *n) {
        const auto original = static_cast<std::size_t>(n - data->nodes);
        if (original >= visited.size() || visited[original]) {
            graph_ok = false;
            return;
        }
        visited[original] = 1;
        indices[n] = static_cast<std::uint32_t>(nodes.size());
        nodes.push_back(n);
        for (std::size_t c = 0; c < n->children_count; ++c)
            visit(n->children[c]);
    };
    const auto &scene = data->scenes[0];
    for (std::size_t i = 0; i < scene.nodes_count; ++i)
        visit(scene.nodes[i]);
    if (!graph_ok || nodes.size() != data->nodes_count)
        return fail(d, "Rig graph must be a tree/forest fully reachable from its one scene.");
    for (std::size_t i = 0; i < data->skins_count; ++i) {
        const auto &s = data->skins[i];
        if (!s.joints_count || s.joints_count > 128)
            return fail(d, "Invalid skin joint count, including unused skin declarations.");
        std::unordered_set<const cgltf_node *> unique;
        for (std::size_t j = 0; j < s.joints_count; ++j)
            if (!indices.contains(s.joints[j]) || !unique.insert(s.joints[j]).second)
                return fail(d, "Invalid or duplicate skin joint.");
        const auto *bind = s.inverse_bind_matrices;
        if (bind &&
            (bind->type != cgltf_type_mat4 || bind->component_type != cgltf_component_type_r_32f ||
             bind->count < s.joints_count))
            return fail(d, "Invalid inverse-bind accessor.");
    }
    presentation::Rig rig;
    rig.id = id;
    std::map<std::uint32_t, std::string> mesh_files;
    std::size_t imported_vertices = 0, imported_indices = 0, imported_keys = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto *n = nodes[i];
        if (n->has_matrix || n->weights_count ||
            (n->has_scale && (n->scale[0] != n->scale[1] || n->scale[0] != n->scale[2])))
            return fail(d, "Rig nodes require uniform TRS; matrices and morph weights are "
                           "not supported.");
        presentation::Node node;
        node.name = n->name && *n->name ? n->name : ("node_" + std::to_string(i));
        node.parent = n->parent ? static_cast<int>(indices.at(n->parent)) : -1;
        if (n->has_translation)
            node.rest.translation = {n->translation[0], n->translation[1], n->translation[2]};
        if (n->has_rotation)
            node.rest.rotation = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
        if (n->has_scale)
            node.rest.scale = n->scale[0];
        if (n->mesh) {
            node.mesh = {derived(id.value, 0x40, static_cast<std::uint32_t>(i) + 1)};
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> triangles;
            presentation::Skin skin;
            skin.node = static_cast<std::uint32_t>(i);
            if (n->skin) {
                const auto &source_skin = *n->skin;
                if (!source_skin.joints_count || source_skin.joints_count > 128)
                    return fail(d, "Skin joint capacity exceeded.");
                const auto *bind = source_skin.inverse_bind_matrices;
                if (bind && (bind->type != cgltf_type_mat4 ||
                             bind->component_type != cgltf_component_type_r_32f ||
                             bind->count < source_skin.joints_count))
                    return fail(d, "Invalid inverse-bind accessor.");
                for (std::size_t j = 0; j < source_skin.joints_count; ++j) {
                    if (!indices.contains(source_skin.joints[j]))
                        return fail(d, "Skin joint is outside the imported scene.");
                    presentation::SkinJoint joint;
                    joint.node = indices.at(source_skin.joints[j]);
                    if (bind &&
                        !cgltf_accessor_read_float(bind, j, joint.inverse_bind.values.data(), 16))
                        return fail(d, "Unreadable inverse bind matrix.");
                    skin.joints.push_back(joint);
                }
            }
            for (std::size_t p = 0; p < n->mesh->primitives_count; ++p) {
                const auto &primitive = n->mesh->primitives[p];
                if (primitive.type != cgltf_primitive_type_triangles || primitive.targets_count ||
                    primitive.has_draco_mesh_compression)
                    return fail(
                        d, "Rig geometry requires uncompressed triangles without morph targets.");
                const cgltf_accessor *positions = nullptr, *normals = nullptr, *colors = nullptr,
                                     *uv = nullptr;
                const cgltf_accessor *joints[2]{}, *weights[2]{};
                for (std::size_t a = 0; a < primitive.attributes_count; ++a) {
                    const auto &attr = primitive.attributes[a];
                    if (attr.type == cgltf_attribute_type_position)
                        positions = attr.data;
                    else if (attr.type == cgltf_attribute_type_normal)
                        normals = attr.data;
                    else if (attr.type == cgltf_attribute_type_color && attr.index == 0)
                        colors = attr.data;
                    else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                        uv = attr.data;
                    else if (attr.type == cgltf_attribute_type_joints ||
                             attr.type == cgltf_attribute_type_weights) {
                        if (!n->skin || attr.index < 0 || attr.index > 1)
                            return fail(d, "Weighted geometry supports JOINTS/WEIGHTS sets 0 and "
                                           "1; extra influences are not truncated.");
                        auto &slot = attr.type == cgltf_attribute_type_joints ? joints[attr.index]
                                                                              : weights[attr.index];
                        if (slot)
                            return fail(d, "Duplicate skin vertex attribute.");
                        slot = attr.data;
                    }
                }
                if (!normals)
                    return fail(d, "Rig geometry requires authored normals.");
                if (!positions || positions->type != cgltf_type_vec3 || positions->count > 100000 ||
                    vertices.size() + positions->count > 100000)
                    return fail(d, "Invalid/excessive rig vertex accessor.");
                if (positions->count > 200000 - imported_vertices)
                    return fail(d, "Total imported vertex budget exceeded.");
                imported_vertices += positions->count;
                if (n->skin) {
                    if (!joints[0] || !weights[0] ||
                        ((joints[1] != nullptr) != (weights[1] != nullptr)) ||
                        positions->count > presentation::max_skin_vertices - skin.vertices.size())
                        return fail(
                            d, "Missing/mismatched skin attributes or vertex budget exceeded.");
                    for (unsigned set = 0; set < 2; ++set)
                        if (joints[set]) {
                            const auto *j = joints[set], *w = weights[set];
                            if (j->type != cgltf_type_vec4 || w->type != cgltf_type_vec4 ||
                                j->count != positions->count || w->count != positions->count ||
                                j->normalized ||
                                (j->component_type != cgltf_component_type_r_8u &&
                                 j->component_type != cgltf_component_type_r_16u) ||
                                !((w->component_type == cgltf_component_type_r_32f &&
                                   !w->normalized) ||
                                  ((w->component_type == cgltf_component_type_r_8u ||
                                    w->component_type == cgltf_component_type_r_16u) &&
                                   w->normalized)))
                                return fail(d, "Invalid skin accessor types/counts/normalization.");
                        }
                }
                float tint[4]{0.65F, 0.72F, 0.82F, 1};
                if (primitive.material) {
                    const auto &m = *primitive.material;
                    if (m.pbr_metallic_roughness.base_color_texture.texture ||
                        m.alpha_mode != cgltf_alpha_mode_opaque)
                        return fail(d, "First character importer supports opaque vertex/base "
                                       "colors; textured/translucent materials are rejected.");
                    std::copy(m.pbr_metallic_roughness.base_color_factor,
                              m.pbr_metallic_roughness.base_color_factor + 4, tint);
                }
                const auto base = static_cast<std::uint32_t>(vertices.size());
                for (std::size_t v = 0; v < positions->count; ++v) {
                    Vertex vertex;
                    if (!cgltf_accessor_read_float(positions, v, vertex.p, 3))
                        return fail(d, "Could not read vertex position.");
                    if (normals && !cgltf_accessor_read_float(normals, v, vertex.n, 3))
                        return fail(d, "Invalid vertex normal.");
                    if (uv && !cgltf_accessor_read_float(uv, v, vertex.uv, 2))
                        return fail(d, "Invalid vertex UV.");
                    float color[4]{1, 1, 1, 1};
                    if (colors && !cgltf_accessor_read_float(colors, v, color, 4))
                        return fail(d, "Invalid vertex color.");
                    for (unsigned k = 0; k < 3; ++k)
                        vertex.color[k] = color[k] * tint[k];
                    for (float x : vertex.p)
                        if (!std::isfinite(x))
                            return fail(d, "Nonfinite vertex.");
                    for (float x : vertex.n)
                        if (!std::isfinite(x))
                            return fail(d, "Nonfinite normal.");
                    for (float x : vertex.uv)
                        if (!std::isfinite(x))
                            return fail(d, "Nonfinite vertex UV.");
                    const double length = std::sqrt(static_cast<double>(vertex.n[0]) * vertex.n[0] +
                                                    static_cast<double>(vertex.n[1]) * vertex.n[1] +
                                                    static_cast<double>(vertex.n[2]) * vertex.n[2]);
                    if (!std::isfinite(length) || length < 1e-12)
                        return fail(d, "Vertex normal must be nonzero.");
                    for (auto &x : vertex.n)
                        x = static_cast<float>(x / length);
                    for (float x : vertex.color)
                        if (!std::isfinite(x) || x < 0 || x > 1)
                            return fail(d, "Invalid vertex color range.");
                    if (n->skin) {
                        presentation::SkinWeights influence;
                        double total = 0;
                        for (unsigned set = 0; set < 2; ++set)
                            if (joints[set]) {
                                cgltf_uint j[4]{};
                                float w[4]{};
                                if (!cgltf_accessor_read_uint(joints[set], v, j, 4) ||
                                    !cgltf_accessor_read_float(weights[set], v, w, 4))
                                    return fail(d, "Unreadable joint weights.");
                                for (unsigned k = 0; k < 4; ++k) {
                                    if (j[k] >= skin.joints.size() || !std::isfinite(w[k]) ||
                                        w[k] < 0 || w[k] > 1)
                                        return fail(d, "Invalid joint index or weight.");
                                    influence.joints[set * 4 + k] =
                                        static_cast<std::uint16_t>(j[k]);
                                    influence.weights[set * 4 + k] = w[k];
                                    total += w[k];
                                }
                            }
                        if (!std::isfinite(total) || std::abs(total - 1) > 0.02)
                            return fail(
                                d, "Skin weights do not sum to one within quantization tolerance.");
                        for (auto &w : influence.weights)
                            w = static_cast<float>(w / total);
                        skin.vertices.push_back(influence);
                    }
                    vertices.push_back(vertex);
                }
                const std::size_t count =
                    primitive.indices ? primitive.indices->count : positions->count;
                if (count % 3 || count > 300000 || triangles.size() + count > 300000)
                    return fail(d, "Invalid triangle index count.");
                if (count > 600000 - imported_indices)
                    return fail(d, "Total imported index budget exceeded.");
                imported_indices += count;
                for (std::size_t k = 0; k < count; ++k) {
                    const auto index =
                        primitive.indices ? cgltf_accessor_read_index(primitive.indices, k) : k;
                    if (index >= positions->count)
                        return fail(d, "Triangle index out of bounds.");
                    triangles.push_back(base + static_cast<std::uint32_t>(index));
                }
            }
            if (vertices.empty() || triangles.empty())
                return fail(d, "Rig mesh is empty.");
            std::ostringstream mesh;
            mesh.imbue(std::locale::classic());
            mesh << std::setprecision(9) << "vice-mesh-source 2\nname node_" << i << "\nid "
                 << node.mesh.value.to_string() << '\n';
            for (const auto &v : vertices)
                mesh << "vertex " << v.p[0] << ' ' << v.p[1] << ' ' << v.p[2] << ' ' << v.color[0]
                     << ' ' << v.color[1] << ' ' << v.color[2] << ' ' << v.n[0] << ' ' << v.n[1]
                     << ' ' << v.n[2] << ' ' << v.uv[0] << ' ' << v.uv[1] << '\n';
            for (std::size_t t = 0; t < triangles.size(); t += 3)
                mesh << "triangle " << triangles[t] << ' ' << triangles[t + 1] << ' '
                     << triangles[t + 2] << '\n';
            mesh_files.emplace(static_cast<std::uint32_t>(i), mesh.str());
            if (n->skin)
                rig.skins.push_back(std::move(skin));
        }
        rig.nodes.push_back(std::move(node));
    }
    for (std::size_t a = 0; a < data->animations_count; ++a) {
        const auto &source_clip = data->animations[a];
        presentation::Clip clip;
        clip.name = source_clip.name && *source_clip.name ? source_clip.name
                                                          : ("clip_" + std::to_string(a));
        if (source_clip.channels_count > 384)
            return fail(d, "Clip track budget exceeded.");
        for (std::size_t c = 0; c < source_clip.channels_count; ++c) {
            const auto &channel = source_clip.channels[c];
            if (!channel.target_node || !channel.sampler || !indices.contains(channel.target_node))
                return fail(d, "Invalid animation target.");
            const auto &sampler = *channel.sampler;
            if (!sampler.input || !sampler.output || sampler.input->count == 0 ||
                sampler.input->count > 4096 ||
                sampler.output->count !=
                    sampler.input->count *
                        (sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1) ||
                sampler.input->type != cgltf_type_scalar)
                return fail(d, "Invalid animation accessors.");
            if (sampler.input->count > 262144 - imported_keys)
                return fail(d, "Total imported key budget exceeded.");
            imported_keys += sampler.input->count;
            presentation::Track track;
            track.node = indices.at(channel.target_node);
            if (sampler.interpolation == cgltf_interpolation_type_step)
                track.interpolation = presentation::Interpolation::step;
            else if (sampler.interpolation == cgltf_interpolation_type_cubic_spline)
                track.interpolation = presentation::Interpolation::cubic_spline;
            else if (sampler.interpolation != cgltf_interpolation_type_linear)
                return fail(d, "Unknown animation interpolation.");
            unsigned components = 3;
            if (channel.target_path == cgltf_animation_path_type_rotation) {
                track.path = presentation::Path::rotation;
                components = 4;
            } else if (channel.target_path == cgltf_animation_path_type_translation)
                track.path = presentation::Path::translation;
            else if (channel.target_path == cgltf_animation_path_type_scale)
                track.path = presentation::Path::scale;
            else
                return fail(d, "Morph animation is not supported.");
            if (sampler.output->type != (components == 4 ? cgltf_type_vec4 : cgltf_type_vec3))
                return fail(d, "Animation output dimensions disagree with target.");
            for (std::size_t k = 0; k < sampler.input->count; ++k) {
                float time = 0;
                presentation::Key key;
                if (!cgltf_accessor_read_float(sampler.input, k, &time, 1) ||
                    !cgltf_accessor_read_float(
                        sampler.output,
                        track.interpolation == presentation::Interpolation::cubic_spline ? 3 * k + 1
                                                                                         : k,
                        key.value.data(), components))
                    return fail(d, "Could not read animation key.");
                if (track.interpolation == presentation::Interpolation::cubic_spline &&
                    (!cgltf_accessor_read_float(sampler.output, 3 * k, key.in_tangent.data(),
                                                components) ||
                     !cgltf_accessor_read_float(sampler.output, 3 * k + 2, key.out_tangent.data(),
                                                components)))
                    return fail(d, "Unreadable cubic spline tangents.");
                key.time = time;
                if (track.path == presentation::Path::scale &&
                    (key.value[0] != key.value[1] || key.value[0] != key.value[2] ||
                     key.in_tangent[0] != key.in_tangent[1] ||
                     key.in_tangent[0] != key.in_tangent[2] ||
                     key.out_tangent[0] != key.out_tangent[1] ||
                     key.out_tangent[0] != key.out_tangent[2]))
                    return fail(d, "Animated rig scale must stay uniform.");
                clip.duration = std::max(clip.duration, key.time);
                track.keys.push_back(key);
            }
            clip.tracks.push_back(std::move(track));
        }
        rig.clips.push_back(std::move(clip));
    }
    if (!presentation::valid(rig, d))
        return false;
    const auto serialized = presentation::serialize_rig(rig);
    if (serialized.empty())
        return fail(d, "Serialized rig exceeds the supported asset budget.");
    const auto temporary =
        output.parent_path() / (".vice-rig-import-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(temporary, ec);
    if (ec)
        return fail(d, "Could not create import staging directory.");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{temporary};
    std::ofstream rig_file(temporary / "character.vrig");
    rig_file << serialized;
    if (!rig_file)
        return fail(d, "Could not stage imported rig.");
    rig_file.close();
    std::ofstream bindings(temporary / "bindings.vworld");
    bindings << "vice-world-source 1\nregion 1 10 0 0 0 0 0 0 full none\nresource "
             << id.value.to_string() << '\n';
    for (const auto &n : rig.nodes)
        if (n.mesh.valid())
            bindings << "resource " << n.mesh.value.to_string() << '\n';
    bindings << "entity " << root.to_string() << " spatial 0 0 0 1 animation "
             << id.value.to_string() << " 0 0 1 1 1\n";
    for (const auto &[index, mesh] : mesh_files) {
        std::ofstream file(temporary / ("node_" + std::to_string(index) + ".vmesh"));
        file << mesh;
        if (!file)
            return fail(d, "Could not stage imported mesh.");
        bindings << "entity " << derived(root, 0x20, index + 1).to_string()
                 << " spatial 0 0 0 1 rig_part " << root.to_string() << ' ' << index
                 << " resource render_mesh " << rig.nodes[index].mesh.value.to_string() << '\n';
    }
    if (!bindings)
        return fail(d, "Could not stage rig bindings.");
    bindings.close();
    if (std::filesystem::exists(output, ec) || ec)
        return fail(d, "Import destination was created concurrently.");
    std::filesystem::rename(temporary, output, ec);
    if (ec)
        return fail(d, "Could not publish imported model directory.");
    return true;
}
} // namespace vice::tools
