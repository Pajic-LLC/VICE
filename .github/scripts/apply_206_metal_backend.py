from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"missing patch context in {path}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


rhi = Path('engine/render/src/metal/rhi.mm')
text = rhi.read_text()

old_validate = '''    for (const auto& resource : resources) {
        if (resource.unbounded) {
            diagnostics =
                "Metal automatic resource layout does not support unbounded ranges. "
                "Use the explicit root-signature conversion path.";
            return false;
        }
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal shader is missing a converter-reflected resource location.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * metal_descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal resource location is too small for the reflected resource array.";
            return false;
        }
    }
'''
new_validate = '''    for (const auto& resource : resources) {
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal shader is missing a converter-reflected resource location.";
            return false;
        }
        if (found->model == MetalShaderResourceBindingModel::descriptor_table) {
            if (found->size != sizeof(std::uint64_t)) {
                diagnostics = "Metal descriptor-table root location must contain one GPU address.";
                return false;
            }
            continue;
        }
        if (resource.unbounded) {
            diagnostics =
                "Metal unbounded ranges require the explicit descriptor-table conversion path.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * metal_descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal resource location is too small for the reflected resource array.";
            return false;
        }
    }
'''
if new_validate not in text:
    if old_validate not in text:
        raise SystemExit('missing Metal shader layout validation block')
    text = text.replace(old_validate, new_validate, 1)

# Graphics and compute pipelines permit any array element for an unbounded logical range.
old_has = '''                return !resource.unbounded && key.array_element < resource.count;
'''
new_has = '''                return resource.unbounded || key.array_element < resource.count;
'''
count = text.count(old_has)
if count != 2:
    raise SystemExit(f'expected 2 Metal has_binding guards, found {count}')
text = text.replace(old_has, new_has)

# Required resources: bounded ranges require all elements; unbounded ranges require a dense non-empty prefix.
old_required = '''    [[nodiscard]] bool all_required_resources_bound() const noexcept {
        if (active_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& resource : active_pipeline_->layout().resources) {
            if (resource.unbounded) {
                return false;
            }
            for (std::uint32_t element = 0; element < resource.count; ++element) {
                const ShaderBindingKey key{
                    .register_class = resource.register_class,
                    .binding = resource.binding,
                    .space = resource.space,
                    .array_element = element,
                };
                if (bound_resource(key) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }
'''
new_required = '''    [[nodiscard]] std::uint32_t bound_prefix_count(
        const PipelineResourceBinding& resource
    ) const noexcept {
        std::uint32_t count = 0U;
        for (const auto& bound : bound_resources_) {
            if (bound.key.register_class != resource.register_class ||
                bound.key.binding != resource.binding || bound.key.space != resource.space) {
                continue;
            }
            if (bound.key.array_element == std::numeric_limits<std::uint32_t>::max()) return 0U;
            count = std::max(count, bound.key.array_element + 1U);
        }
        if (count == 0U) return 0U;
        for (std::uint32_t element = 0U; element < count; ++element) {
            const ShaderBindingKey key{
                .register_class = resource.register_class,
                .binding = resource.binding,
                .space = resource.space,
                .array_element = element,
            };
            if (bound_resource(key) == nullptr) return 0U;
        }
        return count;
    }

    [[nodiscard]] bool all_required_resources_bound() const noexcept {
        if (active_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& resource : active_pipeline_->layout().resources) {
            if (resource.unbounded) {
                if (bound_prefix_count(resource) == 0U) return false;
                continue;
            }
            for (std::uint32_t element = 0; element < resource.count; ++element) {
                const ShaderBindingKey key{
                    .register_class = resource.register_class,
                    .binding = resource.binding,
                    .space = resource.space,
                    .array_element = element,
                };
                if (bound_resource(key) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }
'''
if new_required not in text:
    if old_required not in text:
        raise SystemExit('missing graphics required-resource block')
    text = text.replace(old_required, new_required, 1)

old_compute_required = '''    [[nodiscard]] bool all_required_compute_resources_bound() const noexcept {
        if (active_compute_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& resource : active_compute_pipeline_->layout().resources) {
            if (resource.unbounded) {
                return false;
            }
            for (std::uint32_t element = 0; element < resource.count; ++element) {
                const ShaderBindingKey key{
                    .register_class = resource.register_class,
                    .binding = resource.binding,
                    .space = resource.space,
                    .array_element = element,
                };
                if (compute_bound_resource(key) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }
'''
new_compute_required = '''    [[nodiscard]] std::uint32_t compute_bound_prefix_count(
        const PipelineResourceBinding& resource
    ) const noexcept {
        std::uint32_t count = 0U;
        for (const auto& bound : bound_compute_resources_) {
            if (bound.key.register_class != resource.register_class ||
                bound.key.binding != resource.binding || bound.key.space != resource.space) {
                continue;
            }
            if (bound.key.array_element == std::numeric_limits<std::uint32_t>::max()) return 0U;
            count = std::max(count, bound.key.array_element + 1U);
        }
        if (count == 0U) return 0U;
        for (std::uint32_t element = 0U; element < count; ++element) {
            const ShaderBindingKey key{
                .register_class = resource.register_class,
                .binding = resource.binding,
                .space = resource.space,
                .array_element = element,
            };
            if (compute_bound_resource(key) == nullptr) return 0U;
        }
        return count;
    }

    [[nodiscard]] bool all_required_compute_resources_bound() const noexcept {
        if (active_compute_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& resource : active_compute_pipeline_->layout().resources) {
            if (resource.unbounded) {
                if (compute_bound_prefix_count(resource) == 0U) return false;
                continue;
            }
            for (std::uint32_t element = 0; element < resource.count; ++element) {
                const ShaderBindingKey key{
                    .register_class = resource.register_class,
                    .binding = resource.binding,
                    .space = resource.space,
                    .array_element = element,
                };
                if (compute_bound_resource(key) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }
'''
if new_compute_required not in text:
    if old_compute_required not in text:
        raise SystemExit('missing compute required-resource block')
    text = text.replace(old_compute_required, new_compute_required, 1)

# Replace graphics argument-buffer materialization with automatic + explicit descriptor-table branches.
start = text.index('    [[nodiscard]] bool encode_stage_argument_buffer(const ShaderStage stage) {')
end = text.index('    [[nodiscard]] bool encode_descriptor(', start)
old_function = text[start:end]
new_function = '''    [[nodiscard]] bool encode_stage_argument_buffer(const ShaderStage stage) {
        if (active_pipeline_ == nullptr || device_ == nil || render_encoder_ == nil) {
            return false;
        }
        const auto locations = active_pipeline_->locations(stage);
        if (locations.empty()) {
            return true;
        }

        std::uint64_t argument_buffer_size = 0;
        for (const auto& location : locations) {
            const std::uint64_t end =
                static_cast<std::uint64_t>(location.argument_buffer_offset) + location.size;
            argument_buffer_size = std::max(argument_buffer_size, end);
        }
        if (argument_buffer_size == 0U ||
            argument_buffer_size > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
            return false;
        }

        id<MTLBuffer> argument_buffer = [device_
            newBufferWithLength:static_cast<NSUInteger>(argument_buffer_size)
            options:MTLResourceStorageModeShared];
        if (argument_buffer == nil || argument_buffer.contents == nullptr) {
            core::log(core::LogLevel::error, "Metal could not allocate the converted shader argument buffer.");
            return false;
        }
        std::memset(argument_buffer.contents, 0, static_cast<std::size_t>(argument_buffer_size));
        auto* bytes = static_cast<std::byte*>(argument_buffer.contents);

        const MTLRenderStages render_stage = stage == ShaderStage::vertex
            ? MTLRenderStageVertex
            : MTLRenderStageFragment;

        for (const auto& location : locations) {
            const PipelineResourceBinding* logical = logical_binding(location);
            if (logical == nullptr) return false;

            if (location.model == MetalShaderResourceBindingModel::descriptor_table) {
                const std::uint32_t descriptor_count = logical->unbounded
                    ? bound_prefix_count(*logical)
                    : logical->count;
                if (descriptor_count == 0U ||
                    static_cast<std::uint64_t>(descriptor_count) >
                        std::numeric_limits<NSUInteger>::max() / metal_descriptor_entry_size ||
                    static_cast<std::uint64_t>(location.argument_buffer_offset) + sizeof(std::uint64_t) >
                        argument_buffer_size) {
                    return false;
                }
                const std::uint64_t table_size =
                    static_cast<std::uint64_t>(descriptor_count) * metal_descriptor_entry_size;
                id<MTLBuffer> descriptor_table = [device_
                    newBufferWithLength:static_cast<NSUInteger>(table_size)
                    options:MTLResourceStorageModeShared];
                if (descriptor_table == nil || descriptor_table.contents == nullptr) return false;
                std::memset(descriptor_table.contents, 0, static_cast<std::size_t>(table_size));
                auto* table_entries = static_cast<MetalDescriptorTableEntry*>(descriptor_table.contents);
                for (std::uint32_t element = 0U; element < descriptor_count; ++element) {
                    const ShaderBindingKey key{
                        .register_class = logical->register_class,
                        .binding = logical->binding,
                        .space = logical->space,
                        .array_element = element,
                    };
                    const MetalBoundResource* bound = bound_resource(key);
                    if (bound == nullptr ||
                        !encode_descriptor(table_entries[element], *bound, render_stage)) {
                        return false;
                    }
                }
                const std::uint64_t table_address = descriptor_table.gpuAddress;
                std::memcpy(
                    bytes + static_cast<std::size_t>(location.argument_buffer_offset),
                    &table_address,
                    sizeof(table_address)
                );
                [render_encoder_ useResource:descriptor_table
                                       usage:MTLResourceUsageRead
                                      stages:render_stage];
                [retained_argument_buffers_ addObject:descriptor_table];
                continue;
            }

            if (logical->unbounded) return false;
            const std::uint64_t required =
                static_cast<std::uint64_t>(logical->count) * metal_descriptor_entry_size;
            if (required > location.size) return false;
            for (std::uint32_t element = 0; element < logical->count; ++element) {
                const ShaderBindingKey key{
                    .register_class = logical->register_class,
                    .binding = logical->binding,
                    .space = logical->space,
                    .array_element = element,
                };
                const MetalBoundResource* bound = bound_resource(key);
                if (bound == nullptr) return false;
                const std::uint64_t descriptor_offset =
                    static_cast<std::uint64_t>(location.argument_buffer_offset) +
                    static_cast<std::uint64_t>(element) * metal_descriptor_entry_size;
                if (descriptor_offset + metal_descriptor_entry_size > argument_buffer_size) return false;
                auto* entry = reinterpret_cast<MetalDescriptorTableEntry*>(
                    bytes + static_cast<std::size_t>(descriptor_offset)
                );
                if (!encode_descriptor(*entry, *bound, render_stage)) return false;
            }
        }

        if (stage == ShaderStage::vertex) {
            [render_encoder_ setVertexBuffer:argument_buffer
                                      offset:0
                                     atIndex:metal_shader_converter_argument_buffer_bind_point];
        } else {
            [render_encoder_ setFragmentBuffer:argument_buffer
                                        offset:0
                                       atIndex:metal_shader_converter_argument_buffer_bind_point];
        }
        [retained_argument_buffers_ addObject:argument_buffer];
        return true;
    }

'''
text = text[:start] + new_function + text[end:]

# Replace compute argument-buffer materialization similarly.
start = text.index('    [[nodiscard]] bool encode_compute_argument_buffer(')
end = text.index('    [[nodiscard]] bool encode_compute_descriptor(', start)
old_compute_function = text[start:end]
new_compute_function = '''    [[nodiscard]] bool encode_compute_argument_buffer(
        id<MTLComputeCommandEncoder> encoder
    ) {
        if (active_compute_pipeline_ == nullptr || device_ == nil || encoder == nil) {
            return false;
        }
        const auto locations = active_compute_pipeline_->locations();
        if (locations.empty()) return true;

        std::uint64_t argument_buffer_size = 0U;
        for (const auto& location : locations) {
            const std::uint64_t end =
                static_cast<std::uint64_t>(location.argument_buffer_offset) + location.size;
            argument_buffer_size = std::max(argument_buffer_size, end);
        }
        if (argument_buffer_size == 0U ||
            argument_buffer_size > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
            return false;
        }

        id<MTLBuffer> argument_buffer = [device_
            newBufferWithLength:static_cast<NSUInteger>(argument_buffer_size)
            options:MTLResourceStorageModeShared];
        if (argument_buffer == nil || argument_buffer.contents == nullptr) {
            core::log(
                core::LogLevel::error,
                "Metal could not allocate the converted compute shader argument buffer."
            );
            return false;
        }
        std::memset(argument_buffer.contents, 0, static_cast<std::size_t>(argument_buffer_size));
        auto* bytes = static_cast<std::byte*>(argument_buffer.contents);

        for (const auto& location : locations) {
            const PipelineResourceBinding* logical = compute_logical_binding(location);
            if (logical == nullptr) return false;

            if (location.model == MetalShaderResourceBindingModel::descriptor_table) {
                const std::uint32_t descriptor_count = logical->unbounded
                    ? compute_bound_prefix_count(*logical)
                    : logical->count;
                if (descriptor_count == 0U ||
                    static_cast<std::uint64_t>(descriptor_count) >
                        std::numeric_limits<NSUInteger>::max() / metal_descriptor_entry_size ||
                    static_cast<std::uint64_t>(location.argument_buffer_offset) + sizeof(std::uint64_t) >
                        argument_buffer_size) {
                    return false;
                }
                const std::uint64_t table_size =
                    static_cast<std::uint64_t>(descriptor_count) * metal_descriptor_entry_size;
                id<MTLBuffer> descriptor_table = [device_
                    newBufferWithLength:static_cast<NSUInteger>(table_size)
                    options:MTLResourceStorageModeShared];
                if (descriptor_table == nil || descriptor_table.contents == nullptr) return false;
                std::memset(descriptor_table.contents, 0, static_cast<std::size_t>(table_size));
                auto* table_entries = static_cast<MetalDescriptorTableEntry*>(descriptor_table.contents);
                for (std::uint32_t element = 0U; element < descriptor_count; ++element) {
                    const ShaderBindingKey key{
                        .register_class = logical->register_class,
                        .binding = logical->binding,
                        .space = logical->space,
                        .array_element = element,
                    };
                    const MetalBoundResource* bound = compute_bound_resource(key);
                    if (bound == nullptr ||
                        !encode_compute_descriptor(encoder, table_entries[element], *bound)) {
                        return false;
                    }
                }
                const std::uint64_t table_address = descriptor_table.gpuAddress;
                std::memcpy(
                    bytes + static_cast<std::size_t>(location.argument_buffer_offset),
                    &table_address,
                    sizeof(table_address)
                );
                [encoder useResource:descriptor_table usage:MTLResourceUsageRead];
                [retained_argument_buffers_ addObject:descriptor_table];
                continue;
            }

            if (logical->unbounded) return false;
            const std::uint64_t required =
                static_cast<std::uint64_t>(logical->count) * metal_descriptor_entry_size;
            if (required > location.size) return false;
            for (std::uint32_t element = 0; element < logical->count; ++element) {
                const ShaderBindingKey key{
                    .register_class = logical->register_class,
                    .binding = logical->binding,
                    .space = logical->space,
                    .array_element = element,
                };
                const MetalBoundResource* bound = compute_bound_resource(key);
                if (bound == nullptr) return false;
                const std::uint64_t descriptor_offset =
                    static_cast<std::uint64_t>(location.argument_buffer_offset) +
                    static_cast<std::uint64_t>(element) * metal_descriptor_entry_size;
                if (descriptor_offset + metal_descriptor_entry_size > argument_buffer_size) return false;
                auto* entry = reinterpret_cast<MetalDescriptorTableEntry*>(
                    bytes + static_cast<std::size_t>(descriptor_offset)
                );
                if (!encode_compute_descriptor(encoder, *entry, *bound)) return false;
            }
        }

        [encoder setBuffer:argument_buffer
                    offset:0U
                   atIndex:metal_shader_converter_argument_buffer_bind_point];
        [encoder useResource:argument_buffer usage:MTLResourceUsageRead];
        [retained_argument_buffers_ addObject:argument_buffer];
        return true;
    }

'''
text = text[:start] + new_compute_function + text[end:]

# The explicit cooked layout is now able to carry unbounded pipeline resources.
reject_block = '''        if (std::any_of(
                layout.resources.begin(),
                layout.resources.end(),
                [](const PipelineResourceBinding& resource) { return resource.unbounded; }
            )) {
            core::log(
                core::LogLevel::error,
                "Metal automatic resource layout cannot create a pipeline with unbounded bindings."
            );
            return nullptr;
        }

'''
if reject_block not in text:
    raise SystemExit('missing graphics unbounded pipeline rejection block')
text = text.replace(reject_block, '', 1)
rhi.write_text(text)

# Compute-pipeline location validation mirrors the graphics cooked-layout rules.
compute = Path('engine/render/src/metal/compute_pipeline.mm')
text = compute.read_text()
old = '''    for (const auto& resource : resources) {
        if (resource.unbounded) {
            diagnostics = "Metal automatic compute layout does not support unbounded resource ranges.";
            return false;
        }
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal compute shader is missing a converter-reflected resource location.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal compute resource location is too small for its reflected array.";
            return false;
        }
    }
'''
new = '''    for (const auto& resource : resources) {
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal compute shader is missing a converter-reflected resource location.";
            return false;
        }
        if (found->model == MetalShaderResourceBindingModel::descriptor_table) {
            if (found->size != sizeof(std::uint64_t)) {
                diagnostics = "Metal compute descriptor-table root location must contain one GPU address.";
                return false;
            }
            continue;
        }
        if (resource.unbounded) {
            diagnostics = "Metal unbounded compute ranges require an explicit descriptor table.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal compute resource location is too small for its reflected array.";
            return false;
        }
    }
'''
if new not in text:
    if old not in text:
        raise SystemExit('missing compute Metal location validation block')
    text = text.replace(old, new, 1)
compute.write_text(text)

# Validation coverage: compute pipeline accepts an explicit unbounded logical range.
test = Path('tests/metal_compute_pipeline_tests.mm')
text = test.read_text()
anchor = '''        CHECK(pipeline.diagnostics.empty());

        constexpr NSUInteger element_count = 4U;
'''
insert = '''        CHECK(pipeline.diagnostics.empty());

        constexpr std::array unbounded_resources{
            ShaderResourceBindingDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .count = 0U,
                .unbounded = true,
            },
        };
        constexpr std::array explicit_locations{
            MetalShaderResourceLocationDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .argument_buffer_offset = 0U,
                .size = sizeof(std::uint64_t),
                .model = MetalShaderResourceBindingModel::descriptor_table,
            },
        };
        const ComputePipelineBuildResult explicit_pipeline = build_compute_pipeline(
            device,
            function,
            unbounded_resources,
            explicit_locations,
            group_size
        );
        CHECK(explicit_pipeline.success);
        CHECK(explicit_pipeline.layout.valid);
        CHECK(explicit_pipeline.layout.resources.size() == 1U);
        CHECK(explicit_pipeline.layout.resources[0].unbounded);
        CHECK(explicit_pipeline.locations.size() == 1U);
        CHECK(explicit_pipeline.locations[0].model == MetalShaderResourceBindingModel::descriptor_table);

        constexpr NSUInteger element_count = 4U;
'''
if insert not in text:
    if anchor not in text:
        raise SystemExit('missing Metal compute test anchor')
    text = text.replace(anchor, insert, 1)
test.write_text(text)
