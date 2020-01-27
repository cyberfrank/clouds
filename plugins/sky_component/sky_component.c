#include "sky_component.h"

#include <foundation/api_registry.h>
#include <foundation/the_truth.h>
#include <foundation/the_truth_types.h>
#include <foundation/macros.h>
#include <foundation/allocator.h>
#include <foundation/temp_allocator.h>
#include <foundation/carray.inl>
#include <foundation/math.inl>

#include <plugins/entity/entity.h>
#include <plugins/renderer/renderer.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/resources.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>
#include <plugins/render_graph_toolbox/render_pipeline.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/shader_system/shader_system_api_types.h>
#include <plugins/default_render_pipe/default_render_pipe.h>

#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <the_machinery/component_interfaces/shader_interface.h>

struct tm_the_truth_api *tm_the_truth_api;
struct tm_entity_api *tm_entity_api;
struct tm_allocator_api *tm_allocator_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_renderer_api *tm_renderer_api;
struct tm_render_graph_api *tm_render_graph_api;
struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
struct tm_shader_api *tm_shader_api;
struct tm_shader_system_api *tm_shader_system_api;
struct tm_shader_repository_api *tm_shader_repository_api;
struct tm_api_registry_api *tm_api_registry_api;
struct tm_the_truth_common_types_api *tm_the_truth_common_types_api;

static tm_ci_shader_i *shader_aspect;
static tm_ci_editor_ui_i *editor_aspect;

typedef struct tm_component_manager_o {
    tm_entity_context_o *ctx;
    tm_the_truth_o *tt;
    tm_renderer_backend_i *rb;
    tm_allocator_i allocator;
    tm_shader_repository_o *repo;

    tm_render_graph_module_o *sky_module;
} tm_component_manager_o;

#include "sky_system.inl"

static void update(struct tm_component_manager_o *manager, struct tm_render_graph_o *graph, struct tm_shader_system_context_o *shader_context,
    struct tm_renderer_resource_command_buffer_o *res_buf, const tm_transform_t *transforms, const uint32_t *transform_indices,
    void **component_data, uint32_t num_components, const uint8_t *frustum_visibility)
{
    TM_INIT_TEMP_ALLOCATOR_WITH_ADAPTER(ta, a);

    tm_shader_system_o *sky_system = tm_shader_repository_api->lookup_system(manager->repo, TM_STATIC_HASH("sky_system", 0x5c859d66c1b97863ULL));

    if (!sky_system)
        return;

    tm_shader_io_o *io = tm_shader_api->system_io(sky_system);

    /* carray */ tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, num_components, ta);
    tm_shader_api->create_constant_buffer_instances(io, num_components, cbufs);

    tm_shader_sky_system_t **cdata = (tm_shader_sky_system_t **)component_data;

    for (uint32_t i = 0; i < num_components; ++i)
    {
        tm_shader_system_api->activate_system(shader_context, sky_system, 0, 0, 0, 0);

        tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t) {
            .instance_id = cbufs->instance_id,
            .constant_offset = 0,
            .num_bytes = sizeof(tm_shader_sky_system_t),
            .data = cdata[i],
        }, 1);

        tm_shader_system_api->deactivate_system(shader_context, sky_system);
    }

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "sun_position", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "sun_direction", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "beta_r", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "beta_m", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "mie_g", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__SKY_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    
    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, object_type, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_object_o *writer = tm_the_truth_api->write(tt, component);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__SUN_POSITION, TM_MAKE_VEC3(0, 0, 0), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__SUN_DIRECTION, TM_MAKE_VEC3(0, 1, 0), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__BETA_R, TM_MAKE_VEC3(5.8e-3f, 1.35e-2f, 3.31e-2f), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__BETA_M, TM_MAKE_VEC3(4e-3f, 4e-3f, 4e-3f), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__MIE_G, 0.8f);

    tm_the_truth_api->commit(tt, writer, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_default_object(tt, object_type, component);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, editor_aspect);
}

void load_asset(tm_component_manager_o *manager, tm_entity_t e, void *data, const struct tm_the_truth_o *tt, uint64_t asset)
{
    tm_shader_sky_system_t *c = (tm_shader_sky_system_t *)data;
    const tm_the_truth_object_o *object = tm_the_truth_api->read(tt, asset);

    c->sun_position = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__SUN_POSITION);
    c->sun_direction = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__SUN_DIRECTION);
    c->beta_r = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__BETA_R);
    c->beta_m = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__BETA_M);
    c->mie_g = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__MIE_G);
}

void destroy_component(struct tm_component_manager_o *manager)
{
    tm_entity_context_o *ctx = manager->ctx;
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

void create_component(struct tm_entity_context_o *ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__SKY_COMPONENT, &a);
    tm_component_manager_o *manager = tm_alloc(&a, sizeof(*manager));
    tm_the_truth_o *tt = tm_entity_api->the_truth(ctx);

    uint32_t num_repos;
    tm_shader_repository_o *repo = (tm_shader_repository_o *)(*tm_api_registry_api->implementations(TM_SHADER_REPOSITORY_INSTANCE_NAME, &num_repos));

    *manager = (tm_component_manager_o) {
        .ctx = ctx,
        .tt = tt,
        .allocator = a,
        .repo = repo,
    };

    tm_component_i component = {
        .name = TM_TT_TYPE__SKY_COMPONENT,
        .bytes = sizeof(tm_shader_sky_system_t),
        .manager = (tm_component_manager_o *)manager,
        .load_asset = load_asset,
        .destroy = destroy_component,
    };
    
    tm_entity_api->register_component(ctx, &component);
}

void load_sky_component(struct tm_api_registry_api *reg, bool load)
{
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_the_truth_common_types_api = reg->get(TM_THE_TRUTH_COMMON_TYPES_API_NAME);
    tm_allocator_api = reg->get(TM_ALLOCATOR_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_renderer_api = reg->get(TM_RENDERER_API_NAME);
    tm_render_graph_api = reg->get(TM_RENDER_GRAPH_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_shader_system_api = reg->get(TM_SHADER_SYSTEM_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_api_registry_api = reg->get(TM_API_REGISTRY_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create_component);
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .update = update
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};