#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::rigid_material_cooker {

struct CookResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !bytes.empty();
    }
};

[[nodiscard]] CookResult cook_source_manifest(std::string_view source);

} // namespace vice::tools::rigid_material_cooker
