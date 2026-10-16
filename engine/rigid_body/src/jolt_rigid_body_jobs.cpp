#include <Jolt/Core/JobSystemThreadPool.h>

#include "jolt_job_system.hpp"

namespace JPH {
using ViceJobSystem = ::vice::rigid_body::detail::JoltJobSystem;
} // namespace JPH

#define JobSystemThreadPool ViceJobSystem
#include "jolt_rigid_body.cpp"
#undef JobSystemThreadPool
