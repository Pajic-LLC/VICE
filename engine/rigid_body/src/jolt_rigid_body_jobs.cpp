#include <vice/rigid_body/rigid_body.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>

#include "jolt_job_system.hpp"

namespace JPH {
using ViceJobSystem = ::vice::rigid_body::detail::JoltJobSystem;
} // namespace JPH

#define JobSystemThreadPool ViceJobSystem
#define destroy_body destroy_body_unchecked
#include "jolt_rigid_body.cpp"
#undef destroy_body
#undef JobSystemThreadPool

#include "jolt_constraints.inc"
