namespace {
math::Vec3 vehicle_scale(math::Vec3 v,float s){return {v.x*s,v.y*s,v.z*s};}
math::Vec3 vehicle_add(math::Vec3 a,math::Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
math::Vec3 vehicle_rotate(math::Quat q,math::Vec3 v) {
    const auto m=math::rotation(q);
    return {m(0,0)*v.x+m(0,1)*v.y+m(0,2)*v.z,m(1,0)*v.x+m(1,1)*v.y+m(1,2)*v.z,m(2,0)*v.x+m(2,1)*v.y+m(2,2)*v.z};
}
math::Vec3 vehicle_point_velocity(const BodyState& b,math::Vec3 p) {
    return vehicle_add(b.linear_velocity,math::cross(b.angular_velocity,p-b.position));
}
}
bool AuthoredSimulation::State::drive_vehicles() {
    const float dt=static_cast<float>(coordinator.fixed_delta_seconds());
    for(auto& record:vehicles) {
        const auto& chassis=bodies[body_lookup.at(record.owner)];
        const auto region=world->region(chassis.expected_spatial.region);
        if(!region)return false;
        record.observed={record.expected,0,{}};
        if(region->fidelity==world::RegionFidelity::inactive)continue;
        auto* target=domain_lookup.at(region->id);
        const auto participant=bindings.find(*world,coordinator,record.entity,target->id());
        const auto body=participant?target->body_state(*participant):std::nullopt;
        if(!body || body->mass<=0)return false;
        const auto& v=record.expected;
        const auto up=vehicle_rotate(body->orientation,{0,1,0});
        const auto forward=vehicle_rotate(body->orientation,{0,0,1});
        record.observed.forward_speed=math::dot(body->linear_velocity,forward);
        for(std::size_t wheel=0;wheel<4;++wheel) {
            auto& observed=record.observed.wheels[wheel];
            observed.suspension_length=v.suspension_travel;
            const auto mount=vehicle_add(body->position,vehicle_rotate(body->orientation,
                {(wheel%2==0?-0.5F:0.5F)*v.track,v.mount_height,(wheel<2?0.5F:-0.5F)*v.wheelbase}));
            const auto hit=target->raycast({mount,vehicle_scale(up,-1),v.suspension_travel+v.wheel_radius},
                {.include_static=true,.include_dynamic=true,.include_triggers=false,.exclude_participant=*participant});
            if(!hit || math::dot(hit->normal,up)<0.2F)continue;
            const auto other=participant_bodies.find(hit->participant.value);
            if(other==participant_bodies.end())continue;
            const auto& road=bodies[other->second];
            const auto road_body=target->body_state(hit->participant);
            const auto surface=world->material_surface(road.entity);
            if(!road_body || !surface)return false;
            const auto relative=vehicle_point_velocity(*body,hit->point)-vehicle_point_velocity(*road_body,hit->point);
            const float length=std::clamp(hit->distance-v.wheel_radius,0.0F,v.suspension_travel);
            const float load=std::clamp(v.spring*(v.suspension_travel-length)-v.damping*math::dot(relative,up),0.0F,v.spring*v.suspension_travel*2);
            const float angle=wheel<2?v.steering*v.max_steer:0;
            auto direction=vehicle_rotate(body->orientation,{std::sin(angle),0,std::cos(angle)});
            direction=math::normalize(direction-vehicle_scale(hit->normal,math::dot(direction,hit->normal)));
            const auto side=math::normalize(math::cross(hit->normal,direction));
            const float speed=math::dot(relative,direction),slip=math::dot(relative,side);
            const float drive=v.throttle*v.drive_force*0.25F;
            const float braking=std::clamp(-speed*body->mass/(4*dt)-drive,-v.brake*v.brake_force*0.25F,v.brake*v.brake_force*0.25F);
            float longitudinal=drive+braking;
            float lateral=-slip*std::min(v.lateral_stiffness,body->mass/(4*dt));
            const float demand=std::hypot(longitudinal,lateral);
            const float limit=std::min(10.0F,v.tire_friction*surface->friction)*load;
            if(demand>limit && demand>0){longitudinal*=limit/demand;lateral*=limit/demand;}
            const auto force=vehicle_add(vehicle_scale(hit->normal,load),vehicle_add(vehicle_scale(direction,longitudinal),vehicle_scale(side,lateral)));
            const auto impulse=vehicle_scale(force,dt);
            if(!target->apply_body_impulse(*participant,impulse,hit->point))return false;
            if(road.expected_body.motion==world::RigidMotion::dynamic_body && !target->apply_body_impulse(hit->participant,vehicle_scale(impulse,-1),hit->point))return false;
            observed={true,road.owner,surface->id,
                {region->origin.x+hit->point.x,region->origin.y+hit->point.y,region->origin.z+hit->point.z},
                hit->normal,length,load,longitudinal,lateral};
        }
    }
    return true;
}
bool AuthoredSimulation::State::publish_vehicles() {
    for(auto& record:vehicles) {
        const auto& chassis=bodies[body_lookup.at(record.owner)];
        const auto region=world->region(chassis.expected_spatial.region);
        if(!region)return false;
        auto current=record.expected;
        if(region->fidelity!=world::RegionFidelity::inactive) {
            const auto observed=observed_body(chassis);
            if(!observed)return false;
            const auto velocity=observed->linear_velocity;
            current.distance+=std::hypot(static_cast<double>(velocity.x),static_cast<double>(velocity.y),static_cast<double>(velocity.z))*coordinator.fixed_delta_seconds();
            record.observed.forward_speed=math::dot(velocity,vehicle_rotate(observed->orientation,{0,0,1}));
        }
        if(!world->set_vehicle(record.entity,current))return false;
        record.expected=current;record.observed.state=current;
    }
    vehicle_revision=world->vehicle_revision();return true;
}
VehicleControlResult AuthoredSimulation::control_vehicle(world::PersistentId owner,float throttle,float steering,float brake) {
    if(!std::isfinite(throttle)||!std::isfinite(steering)||!std::isfinite(brake)||std::abs(throttle)>1||std::abs(steering)>1||brake<0||brake>1)return VehicleControlResult::invalid_argument;
    if(!state_->world)return VehicleControlResult::not_found;
    const auto found=state_->vehicle_lookup.find(owner);
    if(found==state_->vehicle_lookup.end())return VehicleControlResult::not_found;
    auto& record=state_->vehicles[found->second];
    const auto& body=state_->bodies[state_->body_lookup.at(owner)];
    if(state_->unpublished_step || state_->world->vehicle_revision()!=state_->vehicle_revision || !state_->matches(body) ||
       state_->world->vehicle(record.entity)!=record.expected)return VehicleControlResult::reset_required;
    const auto region=state_->world->region(body.expected_spatial.region);
    if(!region)return VehicleControlResult::reset_required;
    if(region->fidelity==world::RegionFidelity::inactive)return VehicleControlResult::inactive_region;
    auto current=record.expected;current.throttle=throttle;current.steering=steering;current.brake=brake;
    if(!state_->world->set_vehicle(record.entity,current))return VehicleControlResult::reset_required;
    record.expected=current;record.observed.state=current;state_->vehicle_revision=state_->world->vehicle_revision();
    return VehicleControlResult::applied;
}
std::optional<VehicleObservation> AuthoredSimulation::vehicle(world::PersistentId owner) const noexcept {
    if(!state_->world || state_->unpublished_step || state_->world->vehicle_revision()!=state_->vehicle_revision)return std::nullopt;
    const auto found=state_->vehicle_lookup.find(owner);
    if(found==state_->vehicle_lookup.end())return std::nullopt;
    const auto& record=state_->vehicles[found->second];
    if(state_->world->vehicle(record.entity)!=record.expected || !state_->matches(state_->bodies[state_->body_lookup.at(owner)]))return std::nullopt;
    for(const auto& wheel:record.observed.wheels) if(wheel.grounded) {
        const auto support=state_->body_lookup.find(wheel.supporting_entity);
        if(support==state_->body_lookup.end() || !state_->matches(state_->bodies[support->second]))return std::nullopt;
    }
    auto result=record.observed;
    const auto body=state_->observed_body(state_->bodies[state_->body_lookup.at(owner)]);
    if(!body)return std::nullopt;
    result.forward_speed=math::dot(body->linear_velocity,vehicle_rotate(body->orientation,{0,0,1}));
    return result;
}
std::size_t AuthoredSimulation::vehicle_count() const noexcept{return state_->vehicles.size();}
