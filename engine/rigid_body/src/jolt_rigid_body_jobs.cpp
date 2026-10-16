#include <vice/rigid_body/rigid_body.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>

#include "jolt_job_system.hpp"

namespace JPH {
using ViceJobSystem = ::vice::rigid_body::detail::JoltJobSystem;
} // namespace JPH

#define JobSystemThreadPool ViceJobSystem
#define body_state body_state_unchecked
#define create_body create_body_unchecked
#define create_box create_box_unchecked
#define destroy_body destroy_body_unchecked
#define step step_unchecked
#include "jolt_rigid_body.cpp"
#undef step
#undef destroy_body
#undef create_box
#undef create_body
#undef body_state
#undef JobSystemThreadPool

#include "jolt_motion_quality.inc"

#define step step_constraints_unchecked
#include "jolt_constraints.inc"
#undef step

#include "jolt_character.inc"
