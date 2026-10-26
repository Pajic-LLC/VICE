#include <cassert>

// Compile this guard into every VICE test executable, including Release builds.
#ifdef NDEBUG
#error "VICE tests require active assertions in every build configuration."
#endif
