#include "audio_output.hpp"
namespace vice::presentation {
struct AudioOutput::Impl {};
AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() = default;
bool AudioOutput::start() { return false; }
void AudioOutput::stop() {}
void AudioOutput::push(std::span<const float>) {}
bool AudioOutput::active() const { return false; }
std::uint64_t AudioOutput::delivered() const { return 0; }
std::uint64_t AudioOutput::underruns() const { return 0; }
} // namespace vice::presentation
