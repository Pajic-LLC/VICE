// Frozen ABI 1 prefix. Deliberately does not include today's SDK header.
#include <cstddef>
#include <cstdint>
#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
namespace legacy {
enum class LogLevel : std::uint32_t { debug, info, warning, error };
struct Event;
struct Frame;
struct HostApi {
    std::uint32_t abi_version;
    std::uint32_t size;
    void* context;
    std::size_t (*world_entity_count)(void*) noexcept;
    std::size_t (*world_resource_count)(void*) noexcept;
    void (*log)(void*, LogLevel, const char*) noexcept;
};
struct ModuleApi {
    std::uint32_t abi_version;
    std::uint32_t size;
    void* state;
    bool (*start)(void*, const HostApi*) noexcept;
    void (*event)(void*, const HostApi*, const Event*) noexcept;
    void (*update)(void*, const HostApi*, double) noexcept;
    void (*frame)(void*, const HostApi*, Frame*) noexcept;
    void (*stop)(void*, const HostApi*) noexcept;
    void (*destroy)(void*) noexcept;
};
bool start(void*, const HostApi* host) noexcept { return host->world_entity_count(host->context) == 1; }
void event(void*, const HostApi*, const Event*) noexcept {}
void update(void*, const HostApi*, double) noexcept {}
void frame(void*, const HostApi*, Frame*) noexcept {}
void stop(void*, const HostApi*) noexcept {}
void destroy(void*) noexcept {}
int state = 1;
}
extern "C" EXPORT bool vice_game_module_load(std::uint32_t version, const legacy::HostApi* host, legacy::ModuleApi* output) noexcept {
    if (version != 1 || !host || host->abi_version != 1 || host->size < sizeof(legacy::HostApi) || !output) return false;
    *output = {1, sizeof(legacy::ModuleApi), &legacy::state, legacy::start, legacy::event, legacy::update,
        legacy::frame, legacy::stop, legacy::destroy};
    return true;
}
