namespace {
math::Vec3 attachment_rotate(world::AuthoredRotation q,math::Vec3 v){return vehicle_rotate({q.x,q.y,q.z,q.w},v);}
}
bool AuthoredSimulation::State::controllers_current() const {
    if(!world || unpublished_step || world->attachment_revision()!=attachment_revision || world->character_revision()!=character_revision || world->material_revision()!=material_revision)return false;
    for(const auto& c:characters)if(!matches(c))return false;
    for(const auto& b:bodies)if(!matches(b))return false;
    return true;
}
AttachmentControlResult AuthoredSimulation::attach_character(world::PersistentId owner,world::PersistentId carrier,math::Vec3 offset,float max_distance) {
    using R=AttachmentControlResult;
    const world::CharacterAttachment attachment{carrier,offset};
    if(!world::valid(attachment) || !std::isfinite(max_distance) || max_distance<0 || max_distance>100)return R::invalid_argument;
    if(!state_->world)return R::not_found;
    const auto c=state_->character_lookup.find(owner), b=state_->body_lookup.find(carrier);
    if(c==state_->character_lookup.end() || b==state_->body_lookup.end())return R::not_found;
    if(!state_->controllers_current())return R::reset_required;
    auto& record=state_->characters[c->second];const auto& parent=state_->bodies[b->second];
    if(record.attachment)return R::already_attached;
    if(parent.expected_body.trigger || owner==carrier)return R::invalid_argument;
    const auto region=state_->world->region(parent.expected_spatial.region);
    if(!region || record.expected_spatial.region!=parent.expected_spatial.region)return R::out_of_range;
    if(region->fidelity==world::RegionFidelity::inactive)return R::inactive_region;
    const auto rotated=attachment_rotate(parent.expected_transform.value_or(world::AuthoredTransform{}).rotation,offset);
    const auto p=parent.expected_spatial.global_position,foot=record.expected_spatial.global_position;
    if(std::hypot(p.x+rotated.x-foot.x,p.y+rotated.y-foot.y,p.z+rotated.z-foot.z)>max_distance)return R::out_of_range;
    // All ordinary rejection checks precede either World or native mutation.
    if(!state_->bridge.remove_character(*state_->world,state_->coordinator,record.entity) ||
        !state_->world->set_character_attachment(record.entity,attachment)) {state_->unpublished_step=true;return R::reset_required;}
    record.attachment=attachment;record.expected=*state_->world->character(record.entity);record.expected_spatial=*state_->world->spatial_state(record.entity);
    state_->character_revision=state_->world->character_revision();state_->attachment_revision=state_->world->attachment_revision();return R::applied;
}
AttachmentControlResult AuthoredSimulation::detach_character(world::PersistentId owner,math::Vec3 exit_offset,float max_speed,float max_drop) {
    using R=AttachmentControlResult;
    if(!std::isfinite(exit_offset.x)||!std::isfinite(exit_offset.y)||!std::isfinite(exit_offset.z)||std::hypot(exit_offset.x,exit_offset.y,exit_offset.z)>100 ||
        !std::isfinite(max_speed)||max_speed<0||max_speed>100||!std::isfinite(max_drop)||max_drop<0||max_drop>5)return R::invalid_argument;
    if(!state_->world)return R::not_found;
    const auto found=state_->character_lookup.find(owner);if(found==state_->character_lookup.end())return R::not_found;
    if(!state_->controllers_current())return R::reset_required;
    auto& record=state_->characters[found->second];if(!record.attachment)return R::not_attached;
    const auto parent_index=state_->body_lookup.find(record.attachment->carrier);if(parent_index==state_->body_lookup.end())return R::reset_required;
    const auto& parent=state_->bodies[parent_index->second];const auto region=state_->world->region(parent.expected_spatial.region);
    if(!region || region->id!=record.expected_spatial.region)return R::reset_required;
    if(region->fidelity==world::RegionFidelity::inactive)return R::inactive_region;
    const auto carried_velocity=record.expected.velocity;
    if(std::hypot(carried_velocity.x,carried_velocity.y,carried_velocity.z)>max_speed)return R::blocked;
    auto* target=state_->domain_lookup.at(region->id);
    const auto participant=state_->bindings.find(*state_->world,state_->coordinator,parent.entity,target->id());if(!participant)return R::reset_required;
    const auto offset=attachment_rotate(parent.expected_transform.value_or(world::AuthoredTransform{}).rotation,exit_offset);
    const auto global=parent.expected_spatial.global_position;
    math::Vec3 foot{static_cast<float>(global.x-region->origin.x)+offset.x,static_cast<float>(global.y-region->origin.y)+offset.y,static_cast<float>(global.z-region->origin.z)+offset.z};
    QueryFilter solid{.include_triggers=false};
    solid.excluded_participants=record.collision.excluded_participants;
    const auto ground=target->raycast({vehicle_add(foot,{0,0.05F,0}),{0,-1,0},max_drop+0.05F},solid);
    if(!ground || ground->normal.y<std::cos(record.expected.max_slope_angle))return R::blocked;
    foot.y=ground->point.y+0.01F;
    const world::GlobalPosition destination{region->origin.x+foot.x,region->origin.y+foot.y,region->origin.z+foot.z};
    if(region->bounds && !world::contains(*region,destination))return R::blocked;
    const ShapeId shape{std::numeric_limits<std::uint64_t>::max()};
    if(!target->register_shape(shape,CapsuleShapeDesc{0.5F*(record.expected.height-2*record.expected.radius),record.expected.radius}))return R::reset_required;
    struct Cleanup {Domain* target;ShapeId shape;~Cleanup(){(void)target->unregister_shape(shape);}} cleanup{target,shape};
    const math::Vec3 center=vehicle_add(foot,{0,record.expected.height*0.5F,0});
    for(const auto& hit:target->overlap_shape({shape,center,{}},solid))if(hit.penetration_depth>0.005F)return R::blocked;
    const auto current=state_->world->local_position(record.entity);if(!current)return R::reset_required;
    const math::Vec3 start{current->x,current->y+record.expected.height*0.5F,current->z};
    const auto delta=center-start;const float distance=std::sqrt(math::dot(delta,delta));
    if(distance>0.001F) {
        auto sweep_filter=solid;sweep_filter.exclude_participant=*participant;
        const auto hit=target->sweep_shape({{shape,start,{}},math::normalize(delta),distance},sweep_filter);
        if(hit && hit->distance<distance-0.01F)return R::blocked;
    }
    // Successful preflight makes the walking representation authoritative.
    auto c=record.expected;c.velocity=carried_velocity;c.horizontal_velocity={};c.pending_jump_speed.reset();
    if(!state_->world->clear_character_attachment(record.entity) || !state_->world->set_spatial_state(record.entity,{destination,region->id}) || !state_->world->set_character(record.entity,c)) {state_->unpublished_step=true;return R::reset_required;}
    CharacterDesc desc;desc.id={static_cast<std::uint32_t>(found->second+1)};
    desc.collision=record.collision;
    desc.height=c.height;desc.radius=c.radius;desc.mass=c.mass;desc.max_strength=c.max_strength;desc.max_slope_angle=c.max_slope_angle;desc.step_height=c.step_height;desc.stick_to_floor_distance=c.stick_to_floor_distance;
    if(!state_->bridge.create_character(*state_->world,state_->coordinator,record.entity,desc)){state_->unpublished_step=true;return R::reset_required;}
    const auto native=state_->bridge.character_id(*state_->world,state_->coordinator,record.entity);
    if(!native || !target->set_character_position(*native,foot) || !target->set_character_velocity(*native,c.velocity)){state_->unpublished_step=true;return R::reset_required;}
    record.attachment.reset();record.expected=c;record.expected_spatial={destination,region->id};
    state_->character_revision=state_->world->character_revision();state_->attachment_revision=state_->world->attachment_revision();return R::applied;
}
