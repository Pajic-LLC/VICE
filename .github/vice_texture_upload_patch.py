from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def patch_d3d12() -> None:
    path = Path("engine/render/src/d3d12/rhi.cpp")
    text = path.read_text()

    text = replace_once(
        text,
        "    case TextureFormat::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;\n",
        "    case TextureFormat::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;\n"
        "    case TextureFormat::rgba8_unorm_srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;\n",
        "D3D12 sRGB format",
    )

    text = replace_once(
        text,
        "    D3D12Texture(\n"
        "        const TextureDesc& desc,\n"
        "        ComPtr<ID3D12Resource> resource,\n"
        "        const D3D12_RESOURCE_STATES state,\n"
        "        ComPtr<ID3D12DescriptorHeap> dsv_heap = {}\n"
        "    ) : desc_{desc}, resource_{std::move(resource)}, state_{state}, dsv_heap_{std::move(dsv_heap)} {\n",
        "    D3D12Texture(\n"
        "        const TextureDesc& desc,\n"
        "        ComPtr<ID3D12Device> device,\n"
        "        ComPtr<ID3D12CommandQueue> command_queue,\n"
        "        ComPtr<ID3D12Resource> resource,\n"
        "        const D3D12_RESOURCE_STATES state,\n"
        "        ComPtr<ID3D12DescriptorHeap> dsv_heap = {}\n"
        "    ) : desc_{desc}, device_{std::move(device)}, command_queue_{std::move(command_queue)},\n"
        "        resource_{std::move(resource)}, state_{state}, dsv_heap_{std::move(dsv_heap)} {\n",
        "D3D12 texture constructor",
    )

    marker = (
        "    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }\n"
        "    [[nodiscard]] const TextureDesc& desc() const noexcept { return desc_; }\n"
    )
    method = r'''    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }

    bool write_2d(
        const std::span<const std::byte> data,
        const std::uint64_t bytes_per_row
    ) override {
        if (device_ == nullptr || command_queue_ == nullptr || resource_ == nullptr ||
            desc_.dimension != TextureDimension::d2 || desc_.depth != 1U ||
            desc_.array_layers != 1U || desc_.mip_levels != 1U || desc_.sample_count != 1U ||
            !has_usage(desc_.usage, TextureUsage::copy_destination) ||
            desc_.format == TextureFormat::depth32_float) {
            return false;
        }

        const std::uint64_t bytes_per_pixel = texture_format_bytes_per_pixel(desc_.format);
        if (bytes_per_pixel == 0U ||
            desc_.width > std::numeric_limits<std::uint64_t>::max() / bytes_per_pixel) {
            return false;
        }
        const std::uint64_t minimum_row = static_cast<std::uint64_t>(desc_.width) * bytes_per_pixel;
        if (bytes_per_row < minimum_row ||
            desc_.height > 0U && bytes_per_row >
                std::numeric_limits<std::uint64_t>::max() / desc_.height) {
            return false;
        }
        const std::uint64_t required_source = bytes_per_row * desc_.height;
        if (required_source > data.size()) {
            return false;
        }

        const D3D12_RESOURCE_DESC texture_desc = resource_->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count = 0U;
        UINT64 row_size = 0U;
        UINT64 upload_size = 0U;
        device_->GetCopyableFootprints(
            &texture_desc,
            0U,
            1U,
            0U,
            &footprint,
            &row_count,
            &row_size,
            &upload_size
        );
        if (row_count != desc_.height || row_size < minimum_row || upload_size == 0U ||
            upload_size > std::numeric_limits<std::size_t>::max()) {
            return false;
        }

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CreationNodeMask = 1U;
        upload_heap.VisibleNodeMask = 1U;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1U;
        upload_desc.DepthOrArraySize = 1U;
        upload_desc.MipLevels = 1U;
        upload_desc.SampleDesc.Count = 1U;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        if (FAILED(device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload)
            ))) {
            return false;
        }

        void* mapped = nullptr;
        const D3D12_RANGE read_range{0U, 0U};
        if (FAILED(upload->Map(0U, &read_range, &mapped)) || mapped == nullptr) {
            return false;
        }
        auto* destination = static_cast<std::byte*>(mapped) +
            static_cast<std::size_t>(footprint.Offset);
        for (std::uint32_t row = 0U; row < desc_.height; ++row) {
            std::memcpy(
                destination + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                data.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bytes_per_row),
                static_cast<std::size_t>(minimum_row)
            );
        }
        upload->Unmap(0U, nullptr);

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        if (FAILED(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)
            )) || FAILED(device_->CreateCommandList(
                0U,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&commands)
            ))) {
            return false;
        }

        if (state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource_.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = state_;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commands->ResourceBarrier(1U, &barrier);
        }

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination_location{};
        destination_location.pResource = resource_.Get();
        destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination_location.SubresourceIndex = 0U;
        commands->CopyTextureRegion(&destination_location, 0U, 0U, 0U, &source, nullptr);

        D3D12_RESOURCE_BARRIER final_barrier{};
        final_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        final_barrier.Transition.pResource = resource_.Get();
        final_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        final_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        final_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        commands->ResourceBarrier(1U, &final_barrier);

        if (FAILED(commands->Close())) {
            return false;
        }
        ID3D12CommandList* submitted[] = {commands.Get()};
        command_queue_->ExecuteCommandLists(1U, submitted);

        ComPtr<ID3D12Fence> fence;
        if (FAILED(device_->CreateFence(0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        const HRESULT signal = command_queue_->Signal(fence.Get(), 1U);
        bool completed = SUCCEEDED(signal);
        if (completed && fence->GetCompletedValue() < 1U) {
            completed = SUCCEEDED(fence->SetEventOnCompletion(1U, event)) &&
                WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
        }
        CloseHandle(event);
        if (!completed) {
            return false;
        }
        state_ = D3D12_RESOURCE_STATE_COMMON;
        return true;
    }

    [[nodiscard]] const TextureDesc& desc() const noexcept { return desc_; }
'''
    text = replace_once(text, marker, method, "D3D12 texture write")

    text = replace_once(
        text,
        "private:\n    TextureDesc desc_{};\n    ComPtr<ID3D12Resource> resource_;\n",
        "private:\n    TextureDesc desc_{};\n    ComPtr<ID3D12Device> device_;\n    ComPtr<ID3D12CommandQueue> command_queue_;\n    ComPtr<ID3D12Resource> resource_;\n",
        "D3D12 texture members",
    )

    text = replace_once(
        text,
        "        return std::make_unique<D3D12Texture>(\n"
        "            desc,\n"
        "            std::move(resource),\n"
        "            D3D12_RESOURCE_STATE_COMMON,\n"
        "            std::move(dsv_heap)\n"
        "        );\n",
        "        return std::make_unique<D3D12Texture>(\n"
        "            desc,\n"
        "            device_,\n"
        "            command_queue_,\n"
        "            std::move(resource),\n"
        "            D3D12_RESOURCE_STATE_COMMON,\n"
        "            std::move(dsv_heap)\n"
        "        );\n",
        "D3D12 texture creation",
    )

    path.write_text(text)


def patch_metal() -> None:
    path = Path("engine/render/src/metal/rhi.mm")
    text = path.read_text()

    text = replace_once(
        text,
        "    case TextureFormat::rgba8_unorm: return MTLPixelFormatRGBA8Unorm;\n",
        "    case TextureFormat::rgba8_unorm: return MTLPixelFormatRGBA8Unorm;\n"
        "    case TextureFormat::rgba8_unorm_srgb: return MTLPixelFormatRGBA8Unorm_sRGB;\n",
        "Metal sRGB format",
    )

    text = replace_once(
        text,
        "    MetalTexture(const TextureDesc& desc, id<MTLTexture> texture)\n"
        "        : desc_{desc}, texture_{texture} {\n",
        "    MetalTexture(\n"
        "        const TextureDesc& desc,\n"
        "        id<MTLDevice> device,\n"
        "        id<MTLCommandQueue> command_queue,\n"
        "        id<MTLTexture> texture\n"
        "    ) : desc_{desc}, device_{device}, command_queue_{command_queue}, texture_{texture} {\n",
        "Metal texture constructor",
    )

    marker = (
        "    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }\n"
        "    [[nodiscard]] id<MTLTexture> native() const noexcept { return texture_; }\n"
    )
    method = r'''    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }

    bool write_2d(
        const std::span<const std::byte> data,
        const std::uint64_t bytes_per_row
    ) override {
        if (device_ == nil || command_queue_ == nil || texture_ == nil ||
            desc_.dimension != TextureDimension::d2 || desc_.depth != 1U ||
            desc_.array_layers != 1U || desc_.mip_levels != 1U || desc_.sample_count != 1U ||
            !has_usage(desc_.usage, TextureUsage::copy_destination) ||
            desc_.format == TextureFormat::depth32_float) {
            return false;
        }

        const std::uint64_t bytes_per_pixel = texture_format_bytes_per_pixel(desc_.format);
        if (bytes_per_pixel == 0U ||
            desc_.width > std::numeric_limits<std::uint64_t>::max() / bytes_per_pixel) {
            return false;
        }
        const std::uint64_t minimum_row = static_cast<std::uint64_t>(desc_.width) * bytes_per_pixel;
        if (bytes_per_row < minimum_row ||
            desc_.height > 0U && bytes_per_row >
                std::numeric_limits<std::uint64_t>::max() / desc_.height) {
            return false;
        }
        const std::uint64_t required_source = bytes_per_row * desc_.height;
        if (required_source > data.size()) {
            return false;
        }

        constexpr std::uint64_t row_alignment = 256U;
        if (minimum_row > std::numeric_limits<std::uint64_t>::max() - (row_alignment - 1U)) {
            return false;
        }
        const std::uint64_t staging_row =
            (minimum_row + row_alignment - 1U) / row_alignment * row_alignment;
        if (desc_.height > 0U && staging_row >
            std::numeric_limits<std::uint64_t>::max() / desc_.height) {
            return false;
        }
        const std::uint64_t staging_size = staging_row * desc_.height;
        if (staging_size == 0U || staging_size > std::numeric_limits<NSUInteger>::max()) {
            return false;
        }

        id<MTLBuffer> staging = [device_ newBufferWithLength:static_cast<NSUInteger>(staging_size)
                                                    options:MTLResourceStorageModeShared];
        if (staging == nil || [staging contents] == nullptr) {
            return false;
        }
        auto* destination = static_cast<std::byte*>([staging contents]);
        for (std::uint32_t row = 0U; row < desc_.height; ++row) {
            std::memcpy(
                destination + static_cast<std::size_t>(row) * static_cast<std::size_t>(staging_row),
                data.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bytes_per_row),
                static_cast<std::size_t>(minimum_row)
            );
        }

        id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        if (command_buffer == nil || blit == nil) {
            return false;
        }
        [blit copyFromBuffer:staging
                sourceOffset:0U
           sourceBytesPerRow:static_cast<NSUInteger>(staging_row)
         sourceBytesPerImage:static_cast<NSUInteger>(staging_size)
                  sourceSize:MTLSizeMake(desc_.width, desc_.height, 1U)
                   toTexture:texture_
            destinationSlice:0U
            destinationLevel:0U
           destinationOrigin:MTLOriginMake(0U, 0U, 0U)];
        [blit endEncoding];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        return [command_buffer status] == MTLCommandBufferStatusCompleted;
    }

    [[nodiscard]] id<MTLTexture> native() const noexcept { return texture_; }
'''
    text = replace_once(text, marker, method, "Metal texture write")

    text = replace_once(
        text,
        "private:\n    TextureDesc desc_{};\n    id<MTLTexture> texture_ = nil;\n",
        "private:\n    TextureDesc desc_{};\n    id<MTLDevice> device_ = nil;\n    id<MTLCommandQueue> command_queue_ = nil;\n    id<MTLTexture> texture_ = nil;\n",
        "Metal texture members",
    )

    text = replace_once(
        text,
        "        return std::make_unique<MetalTexture>(desc, texture);\n",
        "        return std::make_unique<MetalTexture>(desc, device_, command_queue_, texture);\n",
        "Metal texture creation",
    )

    path.write_text(text)


patch_d3d12()
patch_metal()
