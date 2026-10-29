#pragma once
#include <cstdint>
#include <memory>
#include <span>
namespace vice::presentation {
class AudioOutput {
  public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput &) = delete;
    bool start();
    void stop();
    void push(std::span<const float> samples);
    bool active() const;
    std::uint64_t delivered() const;
    std::uint64_t underruns() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vice::presentation
