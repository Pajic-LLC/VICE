#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#if defined(__APPLE__)
#define MA_ENABLE_COREAUDIO
#elif defined(_WIN32)
#define MA_ENABLE_WASAPI
#endif
#include "audio_output.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <miniaudio.h>
namespace vice::presentation {
struct AudioOutput::Impl {
    ma_device device{};
    bool initialized = false, running = false;
    static constexpr std::uint64_t capacity = 65536;
    std::array<float, capacity> ring{};
    std::atomic<std::uint64_t> read{0}, write{0}, frames{0}, misses{0};
    static void callback(ma_device *device, void *output, const void *, ma_uint32 count) {
        auto &self = *static_cast<Impl *>(device->pUserData);
        auto *out = static_cast<float *>(output);
        const auto r = self.read.load(std::memory_order_relaxed),
                   w = self.write.load(std::memory_order_acquire);
        const auto available =
            std::min<std::uint64_t>(w - r, static_cast<std::uint64_t>(count) * 2);
        for (std::uint64_t i = 0; i < available; ++i)
            out[i] = self.ring[(r + i) % capacity];
        std::fill(out + available, out + static_cast<std::uint64_t>(count) * 2, 0.0F);
        self.read.store(r + available, std::memory_order_release);
        self.frames.fetch_add(count, std::memory_order_relaxed);
        self.misses.fetch_add(count - available / 2, std::memory_order_relaxed);
    }
};
AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() {
    stop();
    if (impl_->initialized)
        ma_device_uninit(&impl_->device);
}
bool AudioOutput::start() {
    if (impl_->running)
        return true;
    if (!impl_->initialized) {
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.dataCallback = &Impl::callback;
        config.pUserData = impl_.get();
        config.periodSizeInFrames = 512;
        if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS)
            return false;
        impl_->initialized = true;
    }
    impl_->read = 0;
    impl_->write = 4096;
    std::fill(impl_->ring.begin(), impl_->ring.end(), 0.0F);
    impl_->running = ma_device_start(&impl_->device) == MA_SUCCESS;
    return impl_->running;
}
void AudioOutput::stop() {
    if (impl_->running) {
        ma_device_stop(&impl_->device);
        impl_->running = false;
    }
}
void AudioOutput::push(std::span<const float> samples) {
    if (!impl_->running)
        return;
    const auto r = impl_->read.load(std::memory_order_acquire),
               w = impl_->write.load(std::memory_order_relaxed);
    const auto n = std::min<std::uint64_t>(samples.size(), Impl::capacity - (w - r));
    for (std::uint64_t i = 0; i < n; ++i)
        impl_->ring[(w + i) % Impl::capacity] = samples[static_cast<std::size_t>(i)];
    impl_->write.store(w + n, std::memory_order_release);
}
bool AudioOutput::active() const { return impl_->running; }
std::uint64_t AudioOutput::delivered() const {
    return impl_->frames.load(std::memory_order_relaxed);
}
std::uint64_t AudioOutput::underruns() const {
    return impl_->misses.load(std::memory_order_relaxed);
}
} // namespace vice::presentation
