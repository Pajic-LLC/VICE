#include <vice/tools/world_source_transaction.hpp>

#include <vice/platform/file.hpp>
#include <vice/tools/world_cooker.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace vice::tools::world_authoring {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::uint64_t revision_mix = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t revision_prime = 0xbf58476d1ce4e5b9ULL;

void append_hex64(std::string& output, const std::uint64_t value) {
    std::array<char, 16> digits{};
    const auto converted = std::to_chars(
        digits.data(),
        digits.data() + digits.size(),
        value,
        16
    );
    if (converted.ec != std::errc{}) {
        output.append(16U, '0');
        return;
    }
    const auto size = static_cast<std::size_t>(converted.ptr - digits.data());
    output.append(16U - size, '0');
    output.append(digits.data(), converted.ptr);
}

[[nodiscard]] SourceDocument fail_document(std::string diagnostics) {
    return SourceDocument{
        .success = false,
        .diagnostics = std::move(diagnostics),
    };
}

[[nodiscard]] SourceTransactionResult fail_transaction(
    std::string diagnostics,
    std::string revision_before = {}
) {
    return SourceTransactionResult{
        .success = false,
        .revision_before = std::move(revision_before),
        .diagnostics = std::move(diagnostics),
    };
}

class ScopedDirectoryLock {
public:
    ScopedDirectoryLock() = default;
    ScopedDirectoryLock(const ScopedDirectoryLock&) = delete;
    ScopedDirectoryLock& operator=(const ScopedDirectoryLock&) = delete;

    ~ScopedDirectoryLock() {
        if (!held_) return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] bool acquire(
        std::filesystem::path path,
        std::string& diagnostics
    ) {
        std::error_code error;
        const bool created = std::filesystem::create_directory(path, error);
        if (error) {
            diagnostics = "Could not acquire authored World write lock `" +
                path.generic_string() + "`: " + error.message();
            return false;
        }
        if (!created) {
            diagnostics = "Authored World is locked by another VICE authoring transaction: `" +
                path.generic_string() + "`.";
            return false;
        }
        path_ = std::move(path);
        held_ = true;
        diagnostics.clear();
        return true;
    }

private:
    std::filesystem::path path_;
    bool held_ = false;
};

class ScopedTemporaryFile {
public:
    explicit ScopedTemporaryFile(std::filesystem::path path) : path_{std::move(path)} {}
    ScopedTemporaryFile(const ScopedTemporaryFile&) = delete;
    ScopedTemporaryFile& operator=(const ScopedTemporaryFile&) = delete;

    ~ScopedTemporaryFile() {
        if (!remove_) return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    void release() noexcept { remove_ = false; }

private:
    std::filesystem::path path_;
    bool remove_ = true;
};

[[nodiscard]] bool write_staged_source(
    const std::filesystem::path& path,
    const std::string_view source,
    std::string& diagnostics
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        diagnostics = "Could not create staged authored World source `" +
            path.generic_string() + "`.";
        return false;
    }
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    output.flush();
    if (!output) {
        diagnostics = "Could not write staged authored World source `" +
            path.generic_string() + "`.";
        return false;
    }
    output.close();
    if (!output) {
        diagnostics = "Could not close staged authored World source `" +
            path.generic_string() + "`.";
        return false;
    }
    diagnostics.clear();
    return true;
}

[[nodiscard]] std::filesystem::path lock_path_for(
    const std::filesystem::path& source_path
) {
    auto lock = source_path;
    lock += ".vice-write-lock";
    return lock;
}

[[nodiscard]] std::filesystem::path staged_path_for(
    const std::filesystem::path& source_path
) {
    auto staged = source_path;
    staged += ".vice-tmp-" + world::PersistentId::random().to_string();
    return staged;
}

} // namespace

std::string source_revision(const std::string_view source) {
    std::uint64_t first = fnv_offset;
    std::uint64_t second = revision_mix ^ static_cast<std::uint64_t>(source.size());
    std::uint64_t index = 0U;
    for (const unsigned char byte : source) {
        first ^= static_cast<std::uint64_t>(byte);
        first *= fnv_prime;

        second ^= static_cast<std::uint64_t>(byte) + revision_mix + index;
        second *= revision_prime;
        second ^= second >> 29U;
        ++index;
    }

    std::string revision = "vice.world.source.v1-";
    revision.reserve(revision.size() + 32U);
    append_hex64(revision, first);
    append_hex64(revision, second);
    return revision;
}

SourceDocument load_source_document(const std::filesystem::path& source_path) {
    if (source_path.empty()) {
        return fail_document("Authored World source path is empty.");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source_path, error) || error) {
        return fail_document(
            "Authored World source is not a regular file: `" + source_path.generic_string() + "`."
        );
    }

    const auto size = std::filesystem::file_size(source_path, error);
    if (error) {
        return fail_document(
            "Could not inspect authored World source `" + source_path.generic_string() +
            "`: " + error.message()
        );
    }
    if (size > max_authored_world_source_bytes) {
        return fail_document(
            "Authored World source exceeds the VICE authoring limit of " +
            std::to_string(max_authored_world_source_bytes) + " bytes."
        );
    }

    std::ifstream input{source_path, std::ios::binary};
    if (!input) {
        return fail_document(
            "Could not open authored World source `" + source_path.generic_string() + "`."
        );
    }
    std::string source;
    source.resize(static_cast<std::size_t>(size));
    if (size != 0U) {
        input.read(source.data(), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            return fail_document(
                "Could not read complete authored World source `" +
                source_path.generic_string() + "`."
            );
        }
    }

    const auto parsed = world_cooker::parse_source_manifest(source);
    if (!parsed.snapshot.has_value()) {
        return fail_document("Authored World source is invalid: " + parsed.diagnostics);
    }
    const auto revision = source_revision(source);

    return SourceDocument{
        .success = true,
        .snapshot = *parsed.snapshot,
        .source = std::move(source),
        .revision = revision,
    };
}

SourceTransactionResult apply_source_transaction(const SourceTransactionRequest& request) {
    if (request.source_path.empty()) {
        return fail_transaction("Authored World source path is empty.");
    }
    if (request.expected_revision.empty()) {
        return fail_transaction("Authored World source revision precondition is required.");
    }

    ScopedDirectoryLock write_lock;
    std::string lock_diagnostics;
    if (!write_lock.acquire(lock_path_for(request.source_path), lock_diagnostics)) {
        return fail_transaction(std::move(lock_diagnostics));
    }

    auto current = load_source_document(request.source_path);
    if (!current) {
        return fail_transaction(std::move(current.diagnostics));
    }
    if (current.revision != request.expected_revision) {
        return fail_transaction(
            "Authored World source revision is stale; reload before applying edits.",
            current.revision
        );
    }

    const auto edited = apply(current.snapshot, request.transaction);
    if (!edited) {
        return fail_transaction(edited.diagnostics, current.revision);
    }

    const auto next_revision = source_revision(edited.serialized_source);
    if (request.dry_run || !edited.changed) {
        return SourceTransactionResult{
            .success = true,
            .changed = edited.changed,
            .published = false,
            .applied_operations = edited.applied_operations,
            .revision_before = current.revision,
            .revision_after = next_revision,
        };
    }

    const auto staged_path = staged_path_for(request.source_path);
    ScopedTemporaryFile staged_cleanup{staged_path};
    std::string write_diagnostics;
    if (!write_staged_source(staged_path, edited.serialized_source, write_diagnostics)) {
        return fail_transaction(std::move(write_diagnostics), current.revision);
    }

    const auto staged = load_source_document(staged_path);
    if (!staged) {
        return fail_transaction(
            "Staged authored World failed validation: " + staged.diagnostics,
            current.revision
        );
    }
    if (staged.source != edited.serialized_source || staged.revision != next_revision) {
        return fail_transaction(
            "Staged authored World bytes do not match the validated transaction result.",
            current.revision
        );
    }

    // Detect non-VICE writers that changed the source while this transaction
    // was validating/staging. VICE writers are serialized by the sidecar lock.
    const auto before_publish = load_source_document(request.source_path);
    if (!before_publish) {
        return fail_transaction(before_publish.diagnostics, current.revision);
    }
    if (before_publish.revision != current.revision) {
        return fail_transaction(
            "Authored World source changed while the transaction was being prepared; edits were not published.",
            before_publish.revision
        );
    }

    const auto published = platform::atomic_replace_file(staged_path, request.source_path);
    if (!published) {
        return fail_transaction(
            "Could not publish authored World transaction: " + published.diagnostics,
            current.revision
        );
    }
    staged_cleanup.release();

    // The atomic replacement is the transaction commit point. All candidate
    // validation and staged-byte verification occurs before this point so a
    // committed write is never reported as a rejected transaction.
    return SourceTransactionResult{
        .success = true,
        .changed = true,
        .published = true,
        .applied_operations = edited.applied_operations,
        .revision_before = current.revision,
        .revision_after = next_revision,
    };
}

} // namespace vice::tools::world_authoring
