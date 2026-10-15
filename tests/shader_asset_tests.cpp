#include <vice/render/shader_asset.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

class FakeShaderModule final : public vice::render::ShaderModule {
public:
    explicit FakeShaderModule(const vice::render::ShaderStage stage) : stage_{stage} {}
    [[nodiscard]] vice::render::ShaderStage stage() const noexcept override { return stage_; }
private:
    vice::render::ShaderStage stage_;
};

class FakeDevice final : public vice::render::Device {
public:
    explicit FakeDevice(const vice::render::Backend backend) : backend_{backend} {}

    [[nodiscard]] vice::render::Backend backend() const noexcept override { return backend_; }
    [[nodiscard]] vice::render::DeviceCapabilities capabilities() const noexcept override { return {}; }

    [[nodiscard]] std::unique_ptr<vice::render::Buffer> create_buffer(
        const vice::render::BufferDesc&
    ) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<vice::render::Texture> create_texture(
        const vice::render::TextureDesc&
    ) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<vice::render::Sampler> create_sampler(
        const vice::render::SamplerDesc&
    ) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<vice::render::ShaderModule> create_shader_module(
        const vice::render::ShaderModuleDesc&
    ) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<vice::render::ShaderModule> create_shader_module(
        const vice::render::ShaderBinaryDesc& desc
    ) override {
        last_format = desc.format;
        last_stage = desc.stage;
        last_entry_point = std::string{desc.entry_point};
        last_binary_size = desc.binary.size();
        last_resource_count = desc.resources.size();
        last_metal_location_count = desc.metal_resource_locations.size();
        return std::make_unique<FakeShaderModule>(desc.stage);
    }
    [[nodiscard]] std::unique_ptr<vice::render::GraphicsPipeline> create_graphics_pipeline(
        const vice::render::GraphicsPipelineDesc&
    ) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<vice::render::CommandList> create_command_list() override {
        return nullptr;
    }
    vice::render::SubmissionId submit(
        std::unique_ptr<vice::render::CommandList>
    ) override { return vice::render::invalid_submission; }
    [[nodiscard]] vice::render::SubmissionId completed_submission() const noexcept override {
        return vice::render::invalid_submission;
    }
    bool wait_for_submission(vice::render::SubmissionId) override { return false; }
    bool wait_idle() override { return true; }
    [[nodiscard]] std::unique_ptr<vice::render::Swapchain> create_swapchain(
        vice::platform::Window&
    ) override { return nullptr; }

    vice::render::ShaderBinaryFormat last_format = vice::render::ShaderBinaryFormat::dxil;
    vice::render::ShaderStage last_stage = vice::render::ShaderStage::vertex;
    std::string last_entry_point;
    std::size_t last_binary_size = 0;
    std::size_t last_resource_count = 0;
    std::size_t last_metal_location_count = 0;

private:
    vice::render::Backend backend_;
};

void write_text(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void write_binary(const std::filesystem::path& path, const std::string_view content) {
    write_text(path, content);
}

constexpr std::string_view valid_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "vice_main",
  "stage": "fragment",
  "artifacts": {
    "dxil": "shader.dxil",
    "dxc_assembly": "shader.dxc.txt",
    "metallib": "shader.metallib",
    "metal_reflection": "shader.metal.json"
  },
  "metal_binding": {
    "version": 2,
    "model": "automatic_linear",
    "descriptor_entry_size": 24,
    "locations": [
      {
        "class": "constant_buffer",
        "binding": 2,
        "space": 1,
        "argument_buffer_offset": 0,
        "size": 24
      },
      {
        "class": "shader_resource",
        "binding": 4,
        "space": 2,
        "argument_buffer_offset": 24,
        "size": 48
      }
    ]
  },
  "reflection": {
    "version": 1,
    "resources": [
      {
        "name": "FrameData",
        "class": "constant_buffer",
        "binding": 2,
        "space": 1,
        "count": 1,
        "unbounded": false,
        "type": "cbuffer",
        "format": "NA",
        "dimension": "NA"
      },
      {
        "name": "InputTexture",
        "class": "shader_resource",
        "binding": 4,
        "space": 2,
        "count": 2,
        "unbounded": false,
        "type": "texture",
        "format": "f32",
        "dimension": "2d"
      }
    ]
  }
})JSON";

} // namespace

int main() {
    using namespace vice::render;

    const CookedShaderManifest manifest = parse_cooked_shader_manifest(valid_manifest);
    CHECK(manifest.valid);
    CHECK(manifest.diagnostics.empty());
    CHECK(manifest.version == 4U);
    CHECK(manifest.stage == ShaderStage::fragment);
    CHECK(manifest.entry_point == "vice_main");
    CHECK(manifest.dxil_artifact == "shader.dxil");
    CHECK(manifest.metallib_artifact == "shader.metallib");
    CHECK(manifest.resources.size() == 2U);
    CHECK(manifest.metal_resource_locations.size() == 2U);
    CHECK(manifest.resources[1].count == 2U);
    CHECK(manifest.metal_resource_locations[1].argument_buffer_offset == 24U);
    CHECK(manifest.metal_resource_locations[1].size == 48U);

    std::string old_version{valid_manifest};
    const std::size_t version_position = old_version.find("\"version\": 4");
    CHECK(version_position != std::string::npos);
    old_version.replace(version_position, std::string{"\"version\": 4"}.size(), "\"version\": 3");
    const CookedShaderManifest rejected_version = parse_cooked_shader_manifest(old_version);
    CHECK(!rejected_version.valid);
    CHECK(rejected_version.diagnostics.find("version") != std::string::npos);

    std::string unsafe_path{valid_manifest};
    const std::size_t dxil_position = unsafe_path.find("shader.dxil");
    CHECK(dxil_position != std::string::npos);
    unsafe_path.replace(dxil_position, std::string{"shader.dxil"}.size(), "../shader.dxil");
    const CookedShaderManifest rejected_path = parse_cooked_shader_manifest(unsafe_path);
    CHECK(!rejected_path.valid);
    CHECK(rejected_path.diagnostics.find("unsafe") != std::string::npos);

    std::string missing_location{valid_manifest};
    const std::string location = R"JSON(,
      {
        "class": "shader_resource",
        "binding": 4,
        "space": 2,
        "argument_buffer_offset": 24,
        "size": 48
      })JSON";
    const std::size_t location_position = missing_location.find(location);
    CHECK(location_position != std::string::npos);
    missing_location.erase(location_position, location.size());
    const CookedShaderManifest rejected_location = parse_cooked_shader_manifest(missing_location);
    CHECK(!rejected_location.valid);
    CHECK(rejected_location.diagnostics.find("missing") != std::string::npos);

    constexpr std::string_view d3d_only_manifest = R"JSON({
      "format": "vice.shader",
      "version": 4,
      "entry_point": "main",
      "stage": "vertex",
      "artifacts": {
        "dxil": "shader.dxil",
        "dxc_assembly": "shader.dxc.txt",
        "metallib": null,
        "metal_reflection": null
      },
      "metal_binding": {
        "version": 2,
        "model": null,
        "descriptor_entry_size": 24,
        "locations": []
      },
      "reflection": {
        "version": 1,
        "resources": []
      }
    })JSON";
    const CookedShaderManifest d3d_manifest = parse_cooked_shader_manifest(d3d_only_manifest);
    CHECK(d3d_manifest.valid);
    CHECK(d3d_manifest.metallib_artifact.empty());

    const auto directory =
        std::filesystem::temp_directory_path() / "vice_cooked_shader_asset_tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto manifest_path = directory / "shader.vice.json";
    write_text(manifest_path, valid_manifest);
    write_binary(directory / "shader.dxil", "DXIL");
    write_binary(directory / "shader.metallib", "MTLB");

    FakeDevice d3d_device{Backend::direct3d12};
    const CookedShaderLoadResult d3d_result = load_cooked_shader(d3d_device, manifest_path);
    CHECK(static_cast<bool>(d3d_result));
    CHECK(d3d_result.diagnostics.empty());
    CHECK(d3d_device.last_format == ShaderBinaryFormat::dxil);
    CHECK(d3d_device.last_stage == ShaderStage::fragment);
    CHECK(d3d_device.last_entry_point == "vice_main");
    CHECK(d3d_device.last_binary_size == 4U);
    CHECK(d3d_device.last_resource_count == 2U);
    CHECK(d3d_device.last_metal_location_count == 0U);

    FakeDevice metal_device{Backend::metal};
    const CookedShaderLoadResult metal_result = load_cooked_shader(metal_device, manifest_path);
    CHECK(static_cast<bool>(metal_result));
    CHECK(metal_result.diagnostics.empty());
    CHECK(metal_device.last_format == ShaderBinaryFormat::metallib);
    CHECK(metal_device.last_stage == ShaderStage::fragment);
    CHECK(metal_device.last_entry_point == "vice_main");
    CHECK(metal_device.last_binary_size == 4U);
    CHECK(metal_device.last_resource_count == 2U);
    CHECK(metal_device.last_metal_location_count == 2U);

    std::filesystem::remove(directory / "shader.metallib");
    const CookedShaderLoadResult missing_binary = load_cooked_shader(metal_device, manifest_path);
    CHECK(!static_cast<bool>(missing_binary));
    CHECK(!missing_binary.diagnostics.empty());

    std::filesystem::remove_all(directory);
    return 0;
}

#undef CHECK
