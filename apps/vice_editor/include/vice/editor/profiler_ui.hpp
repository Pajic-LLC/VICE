#pragma once

#include <vice/editor/profiler.hpp>

namespace vice::editor {

class ProfilerUi {
public:
    void draw(const ProfilerSnapshot& snapshot, const ProfilerHistory& history);
};

} // namespace vice::editor
