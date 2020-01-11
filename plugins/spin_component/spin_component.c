#include "spin_component.h"

#include <the_machinery/component_interfaces/editor_ui_interface.h>

#include <plugins/entity/entity.h>
#include <plugins/entity/link_component.h>
#include <plugins/entity/transform_component.h>

#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/feature_flags.h>
#include <foundation/localizer.h>
#include <foundation/math.inl>
#include <foundation/the_truth.h>
#include <foundation/the_truth_types.h>

struct tm_entity_api *tm_entity_api;
struct tm_feature_flags_api *tm_feature_flags_api;
struct tm_link_component_api *tm_link_component_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_the_truth_api *tm_the_truth_api;
struct tm_the_truth_common_types_api *tm_the_truth_common_types_api;

// Generated type name, included here for localizer:
// TM_LOCALIZE("Spin Component")

typedef tm_vec3_t tm_spin_component_t;

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i){ 0 };

static void create_truth_types(struct tm_the_truth_o *tt)
{
    // TODO: Should angular velocity be represented with a TM_TT_POSITION type? It is not really
    // a position, so maybe it should be a more generic TM_TT_VEC3 type instead, or a specialized
    // TM_TT_ANGULAR_VELOCITY? Need to think about this some more.
    tm_the_truth_property_definition_t spin_component_properties[] = {
        { "angular_velocity", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__POSITION },
	};

    tm_the_truth_common_types_api->create_common_types(tt);
    const uint64_t spin_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__SPIN_COMPONENT, spin_component_properties, TM_ARRAY_COUNT(spin_component_properties));
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, spin_type);

    tm_the_truth_api->set_aspect(tt, spin_type, TM_CI_EDITOR_UI, editor_aspect);
}

static void load_asset(tm_component_manager_o *man, tm_entity_t e, void *c_vp, const tm_the_truth_o *tt, uint64_t asset)
{
    tm_spin_component_t *c = c_vp;
    *c = tm_the_truth_common_types_api->get_position(tt, tm_tt_read(tt, asset), TM_TT_PROP__SPIN_COMPONENT__ANGULAR_VELOCITY);
}

static void create(struct tm_entity_context_o *ctx)
{
    tm_component_i component = {
        .name = TM_TT_TYPE__SPIN_COMPONENT,
        .bytes = sizeof(tm_spin_component_t),
        .load_asset = load_asset,
    };

    tm_entity_api->register_component(ctx, &component);
}

// Runs on (spin_component, transform_component, link_component)
static void spin_engine_update(tm_engine_o *inst, tm_engine_update_set_t *data)
{
    TM_INIT_TEMP_ALLOCATOR(ta);

    tm_entity_t *mod_link = 0;
    tm_entity_t *mod_transform = 0;

    struct tm_entity_context_o *ctx = (struct tm_entity_context_o *)inst;
    const uint32_t link_component = tm_entity_api->lookup_component(ctx, TM_TT_TYPE_HASH__LINK_COMPONENT);
    void *link_manager = tm_entity_api->component(ctx, link_component)->manager;

    float dt = 1.0f / 60.0f;
    for (const tm_entity_blackboard_value_t *bb = data->blackboard_start; bb != data->blackboard_end; ++bb) {
        if (bb->id == TM_ENTITY_BB__DELTA_TIME)
            dt = (float)bb->double_value;
    }

    for (tm_engine_update_array_t *a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
        tm_spin_component_t *spin = a->components[0];
        tm_transform_component_t *transform = a->components[1];
        tm_link_component_t *link = a->components[2];

        for (uint32_t i = 0; i < a->n; ++i) {
            const float len = tm_vec3_length(spin[i]);
            if (len == 0)
                continue;
            const tm_vec3_t axis = tm_vec3_mul(spin[i], 1.0f / len);
            const float angle = len * dt;
            const tm_vec4_t q = tm_quaternion_from_rotation(axis, angle);

            if (link) {
                link[i].local_transform.rot = tm_quaternion_mul(q, link[i].local_transform.rot);
                tm_carray_temp_push(mod_link, a->entities[i], ta);
            } else {
                transform[i].tm.rot = tm_quaternion_mul(q, transform[i].tm.rot);
                ++transform[i].version;
                tm_carray_temp_push(mod_transform, a->entities[i], ta);
            }
        }
    }

    for (tm_entity_t *e = mod_link; e != tm_carray_end(mod_link); ++e)
        tm_link_component_api->transform(link_manager, *e);

    tm_entity_api->notify(ctx, data->engine->components[1], mod_transform, (uint32_t)tm_carray_size(mod_transform));

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static bool spin_engine_filter(tm_engine_o *inst, const uint32_t *components, uint32_t num_components, const tm_component_mask_t *mask)
{
    return tm_entity_mask_has_component(mask, components[0]) && (tm_entity_mask_has_component(mask, components[1]) || tm_entity_mask_has_component(mask, components[2]));
}

static void register_engine(struct tm_entity_context_o *ctx)
{
    const uint32_t spin_component = tm_entity_api->lookup_component(ctx, TM_TT_TYPE_HASH__SPIN_COMPONENT);
    const uint32_t transform_component = tm_entity_api->lookup_component(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);
    const uint32_t link_component = tm_entity_api->lookup_component(ctx, TM_TT_TYPE_HASH__LINK_COMPONENT);

    const tm_engine_i spin_engine = {
        .name = TM_LOCALIZE_LATER("Spin Component Engine"),
        .num_components = 3,
        .components = { spin_component, transform_component, link_component },
        .writes = { false, true, true },
        .update = spin_engine_update,
        .filter = spin_engine_filter,
        .inst = (tm_engine_o *)ctx,
    };
    tm_entity_api->register_engine(ctx, &spin_engine);
}

struct tm_spin_component_api *tm_spin_component_api = &(struct tm_spin_component_api){
    .create_truth_types = create_truth_types,
    .create = create,
    .register_engine = register_engine,
};

void load_actual_spin_component(struct tm_api_registry_api *reg, bool load)
{
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_feature_flags_api = reg->get(TM_FEATURE_FLAGS_API_NAME);
    tm_link_component_api = reg->get(TM_LINK_COMPONENT_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_the_truth_common_types_api = reg->get(TM_THE_TRUTH_COMMON_TYPES_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);

    tm_set_or_remove_api(reg, load, TM_SPIN_COMPONENT_API_NAME, tm_spin_component_api);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, tm_spin_component_api->create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, tm_spin_component_api->create);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_SIMULATION_INTERFACE_NAME, tm_spin_component_api->register_engine);
}
