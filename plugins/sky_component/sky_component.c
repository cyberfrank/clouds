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
#include <plugins/editor_views/properties.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/shader_system/shader_system.h>

#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <the_machinery/component_interfaces/shader_interface.h>

struct tm_the_truth_api *tm_the_truth_api;
struct tm_entity_api *tm_entity_api;
struct tm_allocator_api *tm_allocator_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_shader_api *tm_shader_api;
struct tm_shader_system_api *tm_shader_system_api;
struct tm_shader_repository_api *tm_shader_repository_api;
struct tm_api_registry_api *tm_api_registry_api;
struct tm_the_truth_common_types_api *tm_the_truth_common_types_api;
struct tm_properties_view_api *tm_properties_view_api;

static tm_ci_shader_i *shader_aspect;
static tm_ci_editor_ui_i *editor_aspect;
static tm_properties_aspect_i *properties_aspect;

typedef struct tm_sky_component_t {
    tm_vec3_t beta_r;
    tm_vec3_t beta_m;
    float mie_g;
    float mie_c;
    float ray_c;
    float turbidity;
    float sun_azimuth;
    float sun_elevation;
    float sun_size;
    float sun_intensity;
} tm_sky_component_t;

typedef struct tm_component_manager_o {
    tm_entity_context_o *ctx;
    tm_the_truth_o *tt;
    tm_allocator_i allocator;
    tm_shader_repository_o *repo;
} tm_component_manager_o;

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "beta_r", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "beta_m", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        { "mie_g", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "mie_c", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "ray_c", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "turbidity", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "sun_size", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "sun_intensity", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__SKY_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    
    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, object_type, TM_TT_NO_UNDO_SCOPE);

    tm_the_truth_object_o *writer = tm_the_truth_api->write(tt, component);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__BETA_R, TM_MAKE_VEC3(5.8e-6f, 1.35e-5f, 3.31e-5f), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, writer, TM_TT_PROP__SKY_COMPONENT__BETA_M, TM_MAKE_VEC3(0.686f, 0.678f, 0.666f), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__MIE_G, 0.8f);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__MIE_C, 0.005f);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__RAY_C, 1.0f);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__TURBIDITY, 2.0f);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__SUN_SIZE, 1.0f);
    tm_the_truth_api->set_float(tt, writer, TM_TT_PROP__SKY_COMPONENT__SUN_INTENSITY, 100.0f);

    tm_the_truth_api->commit(tt, writer, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_default_object(tt, object_type, component);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);
}

static bool set_constant(tm_shader_io_o *io, tm_renderer_resource_command_buffer_o *res_buf, tm_shader_constant_buffer_instance_t *instance,
    uint64_t name, const void *data, uint32_t data_size)
{
    tm_shader_constant_t constant;
    uint32_t constant_offset;
    if (tm_shader_api->lookup_constant(io, name, &constant, &constant_offset)) {
        tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t) {
            .instance_id = instance->instance_id,
            .constant_offset = constant_offset,
            .num_bytes = data_size,
            .data = data,
        }, 1);
        return true;
    }
    return false;
}

static void update(struct tm_component_manager_o *manager, struct tm_render_graph_o *graph, struct tm_shader_system_context_o *shader_context,
    struct tm_renderer_resource_command_buffer_o *res_buf, const tm_transform_t *transforms, const uint32_t *transform_indices,
    void **component_data, uint32_t num_components, const uint8_t *frustum_visibility)
{
    TM_INIT_TEMP_ALLOCATOR(ta);

    tm_shader_system_o *sky_system = tm_shader_repository_api->lookup_system(manager->repo, TM_STATIC_HASH("sky_system", 0x5c859d66c1b97863ULL));

    if (!sky_system)
        return;

    tm_shader_io_o *io = tm_shader_api->system_io(sky_system);

    /* carray */ tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, num_components, ta);
    tm_shader_api->create_constant_buffer_instances(io, num_components, cbufs);

    tm_sky_component_t **cdata = (tm_sky_component_t **)component_data;
    tm_shader_system_api->activate_system(shader_context, sky_system, cbufs, num_components, 0, 0);

    for (uint32_t i = 0; i < num_components; ++i)
    {
        tm_sky_component_t *c = cdata[i];

        // calculate sun direction
        tm_transform_t transform = transforms[transform_indices[i]];
        tm_mat44_t mat;
        tm_mat44_from_translation_quaternion_scale(&mat, transform.pos, transform.rot, transform.scl);
        tm_vec3_t *sun_dir = tm_mat44_z(&mat);

        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("sun_direction", 0x93b473247f3ee6dfULL), sun_dir, sizeof(tm_vec3_t));

        // reyleigh term
        tm_vec3_t beta_r = tm_vec3_mul(c->beta_r, c->ray_c);
        // mie term
        tm_vec3_t beta_m = tm_vec3_mul(c->beta_m, c->mie_c);

        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("sun_intensity", 0x74c0cbfcc02a44efULL), &c->sun_intensity, sizeof(float));
        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("sun_size", 0x979a4915e2986efdULL), &c->sun_size, sizeof(float));
        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("beta_r", 0x901e98f4de51f4c1ULL), &beta_r, sizeof(tm_vec3_t));
        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("beta_m", 0x23064f493604e286ULL), &beta_m, sizeof(tm_vec3_t));
        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("mie_g", 0x34bf34b177ceac3fULL), &c->mie_g, sizeof(float));  
        set_constant(io, res_buf, &cbufs[i], TM_STATIC_HASH("turbidity", 0x8cda52201e65a661ULL), &c->turbidity, sizeof(float));

    }
    // tm_shader_system_api->deactivate_system(shader_context, sky_system);
    tm_shader_api->destroy_constant_buffer_instances(io, cbufs, num_components);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

void load_asset(tm_component_manager_o *manager, tm_entity_t e, void *data, const struct tm_the_truth_o *tt, uint64_t asset)
{
    tm_sky_component_t *c = (tm_sky_component_t *)data;
    const tm_the_truth_object_o *object = tm_the_truth_api->read(tt, asset);

    c->beta_r = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__BETA_R);
    c->beta_m = tm_the_truth_common_types_api->get_vec3(tt, object, TM_TT_PROP__SKY_COMPONENT__BETA_M);
    c->mie_g = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__MIE_G);
    c->mie_c = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__MIE_C);
    c->ray_c = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__RAY_C);
    c->turbidity = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__TURBIDITY);
    c->sun_size = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__SUN_SIZE);
    c->sun_intensity = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__SKY_COMPONENT__SUN_INTENSITY);
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
        .bytes = sizeof(tm_sky_component_t),
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
    tm_properties_view_api = reg->get(TM_PROPERTIES_VIEW_API_NAME);
    tm_allocator_api = reg->get(TM_ALLOCATOR_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_shader_system_api = reg->get(TM_SHADER_SYSTEM_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_api_registry_api = reg->get(TM_API_REGISTRY_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create_component);
}

static float properties_ui(struct tm_properties_ui_args_t *args, tm_rect_t item_rect, uint64_t object, uint32_t indent)
{
    tm_properties_float_slider_t slider020 = {
        .min = 0.0f,
        .max = 30.0f,
        .step = 0.1f,
        .show_edit_box = true,
    };
    
    item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Turbidity", 0, object, TM_TT_PROP__SKY_COMPONENT__TURBIDITY, &slider020);

    bool atmosphere_settings = true;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Sky Settings", 0, object, indent, true, &atmosphere_settings);
    if (atmosphere_settings)
    {
        const tm_the_truth_object_o *asset_obj = tm_the_truth_api->read(args->tt, object);
        uint64_t beta_r = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__SKY_COMPONENT__BETA_R);
        uint64_t beta_m = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__SKY_COMPONENT__BETA_M);
        
        item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Beta R", 0, beta_r);
        item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Beta M", 0, beta_m);
        item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Mie Scale", 0, object, TM_TT_PROP__SKY_COMPONENT__MIE_C, 0);
        item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Rayleigh Scale", 0, object, TM_TT_PROP__SKY_COMPONENT__RAY_C, 0);
        item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Mie G", 0, object, TM_TT_PROP__SKY_COMPONENT__MIE_G, 0);
    }

    bool sun_settings = true;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Sun Settings", 0, object, indent, true, &sun_settings);
    if (sun_settings)
    {
        item_rect.x += TM_PROPERTIES_METRIC_INDENT;
        item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Sun Size", 0, object, TM_TT_PROP__SKY_COMPONENT__SUN_SIZE, 0);
        item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Sun Intensity", 0, object, TM_TT_PROP__SKY_COMPONENT__SUN_INTENSITY, 0);
    }

    return item_rect.y;
}

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i) {
    .custom_ui = properties_ui,
};

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .update = update
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};