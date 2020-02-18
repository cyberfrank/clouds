#include "cloud_component.h"

#include <foundation/api_registry.h>
#include <foundation/the_truth.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/allocator.h>
#include <foundation/temp_allocator.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/buffer_format.h>
#include <foundation/carray.inl>
#include <foundation/math.inl>

#include <plugins/entity/entity.h>
#include <plugins/editor_views/properties.h>
#include <plugins/creation_graph/creation_graph.h>
#include <plugins/creation_graph/image_nodes.h>
#include <plugins/renderer/renderer.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/resources.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>
#include <plugins/render_graph_toolbox/render_pipeline.h>
#include <plugins/default_render_pipe/default_render_pipe.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/shader_system/shader_system_api_types.h>
#include <plugins/ui/ui.h>

#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <the_machinery/component_interfaces/shader_interface.h>

#include <plugins/creation_graph/creation_graph_output.inl>

struct tm_logger_api *tm_logger_api;
struct tm_the_truth_api *tm_the_truth_api;
struct tm_entity_api *tm_entity_api;
struct tm_renderer_api *tm_renderer_api;
struct tm_render_graph_module_api *tm_render_graph_module_api;
struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
struct tm_properties_view_api *tm_properties_view_api;
struct tm_allocator_api *tm_allocator_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_creation_graph_api *tm_creation_graph_api;
struct tm_ui_api *tm_ui_api;
struct tm_api_registry_api *tm_api_registry_api;
struct tm_shader_api *tm_shader_api;
struct tm_shader_system_api *tm_shader_system_api;
struct tm_shader_repository_api *tm_shader_repository_api;
struct tm_buffer_format_api *tm_buffer_format_api; 
struct tm_render_graph_setup_api *tm_render_graph_setup_api;
struct tm_render_graph_execute_api *tm_render_graph_execute_api;

static tm_ci_shader_i *shader_aspect;
static tm_ci_editor_ui_i *editor_aspect;
static tm_properties_aspect_i *properties_aspect;
struct tm_render_graph_editor_submenu_i *tm_render_graph_editor_submenu_i;

typedef struct tm_cloud_component_t {
    tm_shader_o *shader;
    tm_shader_constant_buffer_instance_t cbuffer;
    tm_shader_resource_binder_instance_t rbinder;

    // TM_CLOUD_QUALITY_*
    uint32_t quality;

    float coverage;
    float cloud_type;
    float precipitation;
    float wind_direction;
    float wind_speed;

    // planet settings
    uint32_t planet_radius;
    uint32_t atmosphere_start;
    uint32_t atmosphere_thickness;

    // volume textures
    struct
    {
        uint64_t asset;
        uint64_t version;
    } textures;

    // weather map
    struct
    {
        uint64_t asset;
        uint64_t version;
    } weather_map;

} tm_cloud_component_t;

typedef struct tm_component_manager_o
{
    tm_entity_context_o *ctx;
    tm_the_truth_o *tt;
    tm_renderer_backend_i *rb;
    tm_allocator_i allocator;
    tm_shader_repository_o *repo;

    tm_render_graph_module_o *module;
    tm_cloud_component_t *active_cloud_component;

    struct
    {
        bool half_resolution;
        bool visualize_weather;
    } settings;

} tm_component_manager_o;

static void editor_submenu(void *inst, struct tm_ui_o *ui, const struct tm_ui_style_t *uistyle, tm_vec2_t submenu_pos)
{
    tm_component_manager_o *manager = (tm_component_manager_o *)inst;

    const tm_ui_menu_item_t items[] = {
        { .text = "Half Resolution", .toggle = &manager->settings.half_resolution },
        { .text = "Show Weather Texture", .toggle = &manager->settings.visualize_weather },
    };

    const tm_ui_menu_t menu = {
        .pos = submenu_pos,
        .items = items,
        .num_items = TM_ARRAY_COUNT(items),
    };

    tm_ui_menu_result_t res = tm_ui_api->menu(ui, uistyle, &menu);
    (void)res;
}

#define TM_VOLUMETRIC_CLOUDS_TARGET TM_STATIC_HASH("volumetric_clouds_target", 0xcb5166cb57718153ULL)
#define TM_VOLUMETRIC_CLOUDS_HISTORY TM_STATIC_HASH("volumetric_clouds_copy", 0x1f3d3166df9e7926ULL)

typedef struct cloud_pass_runtime_data_t
{
    tm_shader_o *shader;
    tm_shader_constant_buffer_instance_t cbuffer;
    tm_shader_resource_binder_instance_t rbinder;
} cloud_pass_runtime_data_t;

static void cloud_pass_setup(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    struct tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;
    tm_cloud_component_t *c = manager->active_cloud_component;
    bool active = c != 0;

    tm_render_graph_setup_api->set_active(graph_setup, active);
    if (!active)
        return;

    float scale = manager->settings.half_resolution ? 0.5f : 1.0f;

    tm_pass_render_target_t color_targets[TM_MAX_RENDER_TARGETS] = {
        { 
            .name = TM_VOLUMETRIC_CLOUDS_TARGET, 
            .create = true, 
            .inherits = TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION, 
            .load_op = TM_RENDERER_RESOURCE_LOAD_OP_DISCARD,
            .scale = { scale, scale, 1.0f },
        }
    };

    tm_render_graph_toolbox_api->setup_render_target_writes(color_targets, 0, graph_setup);

    tm_render_graph_handle_t handle = tm_render_graph_setup_api->external_resource(graph_setup, TM_VOLUMETRIC_CLOUDS_HISTORY);
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, handle, TM_RENDERER_RESOURCE_STATE_RESOURCE);

    cloud_pass_runtime_data_t *rdata = (cloud_pass_runtime_data_t*)runtime_data;
    *rdata = (cloud_pass_runtime_data_t) {
        .shader = c->shader,
        .cbuffer = c->cbuffer,
        .rbinder = c->rbinder,
    };
}

static void cloud_pass_execute(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    cloud_pass_runtime_data_t *rdata = (cloud_pass_runtime_data_t *)runtime_data;

    tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

    tm_renderer_draw_call_info_t draw_call = {
        .primitive_type = TM_RENDERER_PRIMITIVE_TYPE_TRIANGLE_LIST,
        .draw_type = TM_RENDERER_DRAW_TYPE_NON_INDEXED,
        .non_indexed.first_vertex = 0,
        .non_indexed.num_vertices = 3,
        .num_instances = 1,
    };

    tm_shader_io_o *io = tm_shader_api->shader_io(rdata->shader);

    tm_shader_resource_t resource;
    uint32_t slot;
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("history", 0xee0b17af3674bb0fULL), &resource, &slot)) {
        //
        // tm_shader_api->update_resources(tm_shader_api->shader_io(rdata->shader), res_buf, &(tm_shader_resource_update_t){ 
        //     .instance_id = rdata->rbinder.instance_id, 
        //     .resource_slot = slot, 
        //     .first_resource = 0, 
        //     .num_resources = 1, 
        //     .resources = 0,
        //     .resources_view_aspect_flags = 0 
        // }, 1);
    }

    tm_renderer_shader_info_t shader_info;
    if (tm_shader_api->assemble_shader_infos(rdata->shader, 0, 0, tm_render_graph_execute_api->shader_context(graph_execute), res_buf, &rdata->cbuffer, &rdata->rbinder, 1, &shader_info)) {
        tm_renderer_command_buffer_o *cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
        tm_renderer_api->tm_renderer_command_buffer_api->draw_calls(cmd_buf, &sort_key, &draw_call, &shader_info, 1);
    }
}

static tm_render_graph_module_o *create_volumetric_cloud_module(tm_component_manager_o *manager)
{
    tm_render_graph_module_o *mod = tm_render_graph_module_api->create(&manager->allocator);

    tm_render_graph_pass_i pass = {
        .api = {
            .setup_pass = cloud_pass_setup,
            .execute_pass = cloud_pass_execute 
        },
        .profiling_scope = "Volumetric Clouds",
        .const_data = &manager, 
        .const_data_size = sizeof(tm_component_manager_o *),
        .runtime_data_size = sizeof(cloud_pass_runtime_data_t)  
    };
    tm_render_graph_module_api->add_pass(mod, &pass);

    tm_render_graph_toolbox_api->fullscreen_pass(mod, &(tm_fullscreen_pass_setup_t){
        .color_targets = {
            { .name = TM_VOLUMETRIC_CLOUDS_HISTORY, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_DISCARD }
        },
        .shader = TM_STATIC_HASH("copy", 0xd996b7bdf040ea8bULL),
        .input_slots = {
            { .slot_name = TM_STATIC_HASH("textures", 0x64e318ce6361e6acULL), .resources = { { TM_VOLUMETRIC_CLOUDS_TARGET } } }
        },
        .output_pass = true
    }, "Copy");

    tm_render_graph_toolbox_api->fullscreen_pass(mod, &(tm_fullscreen_pass_setup_t){
        .color_targets = {
            { .name = TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_LOAD }
        },
        .depth_stencil_target = {
            .name = TM_DEFAULT_RENDER_PIPE_MAIN_DEPTH_STENCIL_TARGET, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_LOAD,
        },
        .shader = TM_STATIC_HASH("reproject", 0xafe2313fa2c033b7ULL),
        .input_slots = {
            { .slot_name = TM_STATIC_HASH("textures", 0x64e318ce6361e6acULL), .resources = { { TM_VOLUMETRIC_CLOUDS_TARGET }, { TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION } } }
        }
    }, "Reproject");

    tm_render_graph_module_api->set_editor_submenu_interface(mod, &(tm_render_graph_module_editor_submenu_i){
        .submenu_name = "Cloud Module",
        .inst = manager,
        .submenu = editor_submenu,
    });

    return mod;
}

static void graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
    tm_render_graph_module_api->create_persistent_gpu_image(module, TM_VOLUMETRIC_CLOUDS_HISTORY, 0, TM_VOLUMETRIC_CLOUDS_TARGET);
    tm_render_graph_module_api->insert_extension(module, TM_DEFAULT_RENDER_PIPE_MAIN_EXTENSION_SKYDOME, manager->module);
}

static bool set_constant(tm_shader_io_o *io, tm_renderer_resource_command_buffer_o *res_buf, tm_shader_constant_buffer_instance_t *instance,
    uint64_t name, const void *data, uint32_t data_size)
{
    tm_shader_constant_t constant;
    uint32_t constant_offset;
    if (tm_shader_api->lookup_constant(io, name, &constant, &constant_offset)) {
        tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t) { .instance_id = instance->instance_id, .constant_offset = constant_offset, .num_bytes = data_size, .data = data }, 1);
        return true;
    }
    return false;
}

static bool set_resource(tm_shader_io_o *io, tm_renderer_resource_command_buffer_o *res_buf, tm_shader_resource_binder_instance_t *instance,
    uint64_t name, const uint32_t *resource_handle, const uint32_t *aspect_flags, uint32_t first_resource, uint32_t n_resources)
{
    tm_shader_resource_t resource;
    uint32_t resource_slot;
    if (tm_shader_api->lookup_resource(io, name, &resource, &resource_slot)) {
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = instance->instance_id, .resource_slot = resource_slot, .first_resource = first_resource, .num_resources = n_resources, .resources = resource_handle, .resources_view_aspect_flags = aspect_flags }, 1);
        return true;
    }
    return false;
}

static void update(struct tm_component_manager_o *manager, struct tm_render_graph_o *graph, struct tm_shader_system_context_o *shader_context,
    struct tm_renderer_resource_command_buffer_o *res_buf, const tm_transform_t *transforms, const uint32_t *transform_indices,
    void **component_data, uint32_t num_components, const uint8_t *frustum_visibility)
{
    tm_cloud_component_t **cdata = (tm_cloud_component_t **)component_data;
    manager->active_cloud_component = num_components > 0 ? cdata[0] : 0;

    for (uint32_t i = 0; i < num_components; ++i) {
        tm_cloud_component_t *c = cdata[i];
        if (!c->shader)
            continue;

        tm_shader_io_o *io = tm_shader_api->shader_io(c->shader);

        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("planet_radius", 0x5849f79517d87c7ULL), &c->planet_radius, sizeof(uint32_t));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("atmosphere_start", 0xb5f14bebd2751c08ULL), &c->atmosphere_start, sizeof(uint32_t));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("atmosphere_thickness", 0xfa5152e92fe4bd5eULL), &c->atmosphere_thickness, sizeof(uint32_t));
        
        uint32_t num_steps = (c->quality + 1) << 3;
        if (c->quality == TM_CLOUD_QUALITY_GROUND_TRUTH)
            num_steps = 256;

        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("num_steps", 0x55f5f64778324518ULL), &num_steps, sizeof(uint32_t));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("coverage", 0x6dde46384338bcacULL), &c->coverage, sizeof(float));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("precipitation", 0x30d73ebc265650e0ULL), &c->precipitation, sizeof(float));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("cloud_type", 0x3beb28659423dab6ULL), &c->cloud_type, sizeof(float));

        tm_vec2_t wind_vector;
        float wind_speed = c->wind_speed * 0.1f;
        float radians = c->wind_direction * (2.0f * TM_PI);
        wind_vector.x = cosf(radians);
        wind_vector.y = sinf(radians);
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("wind_vector", 0x6874c809dadb28aaULL), &wind_vector, sizeof(tm_vec2_t));
        set_constant(io, res_buf, &c->cbuffer, TM_STATIC_HASH("wind_speed", 0xfd56ca9cad86b08fULL), &wind_speed, sizeof(float));    

        if (c->weather_map.asset) {
            uint64_t wanted_version = tm_creation_graph_api->graph_version(c->weather_map.asset);
            if (wanted_version != c->weather_map.version) {
                tm_creation_graph_output_t image_output = tm_creation_graph_api->output(manager->tt, c->weather_map.asset, TM_CREATION_GRAPH__IMAGE__OUTPUT_NODE_HASH, wanted_version, manager->rb, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, 0, 0);
                c->weather_map.version = image_output.version;

                tm_creation_graph_image_data_t *image = (tm_creation_graph_image_data_t *)image_output.output;
                set_resource(io, res_buf, &c->rbinder, TM_STATIC_HASH("weather_map", 0x95323a8725dcd458ULL), &image->handle, 0, 0, 1);
            }
        }

        if (c->textures.asset) {
            uint64_t wanted_version = tm_creation_graph_api->graph_version(c->textures.asset);
            if (wanted_version != c->textures.version) {
                tm_creation_graph_output_t image_output = tm_creation_graph_api->output(manager->tt, c->textures.asset, TM_CREATION_GRAPH__IMAGE__OUTPUT_NODE_HASH, wanted_version, manager->rb, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, 0, 0);
                c->textures.version = image_output.version;

                tm_creation_graph_image_data_t *images = (tm_creation_graph_image_data_t *)image_output.output;
                if (image_output.num_output_objects == 2)
                {
                    set_resource(io, res_buf, &c->rbinder, TM_STATIC_HASH("cloud_shape", 0xecbca377104137c2ULL), &images[0].handle, 0, 0, 1);
                    set_resource(io, res_buf, &c->rbinder, TM_STATIC_HASH("cloud_detail", 0x7955716250d5b1bcULL), &images[1].handle, 0, 0, 1);
                }
            }
        }
    }
}

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "quality", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "coverage", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "precipitation", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "cloud_type", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "textures", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
        { "weather_map", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
        { "wind_direction", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "wind_speed", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "planet_radius", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "atmosphere_start", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "atmosphere_thickness", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CLOUD_COMPONENT, properties, TM_ARRAY_COUNT(properties));

    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__CLOUD_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_object_o *component_w = tm_the_truth_api->write(tt, component);

    tm_the_truth_api->set_uint32_t(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__QUALITY, TM_CLOUD_QUALITY_HIGH);
    tm_the_truth_api->set_uint32_t(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__PLANET_RADIUS, 6371000);
    tm_the_truth_api->set_uint32_t(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_START, 1500);
    tm_the_truth_api->set_uint32_t(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_THICKNESS, 2000);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, 0.0f);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__PRECIPITATION, 1.0f);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__CLOUD_COMPONENT__CLOUD_TYPE, 0.5f);
    
    tm_the_truth_api->commit(tt, component_w, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_default_object(tt, object_type, component);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);

    tm_the_truth_api->set_property_aspect(tt, object_type, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES, TM_TT_PROP_ASPECT__PROPERTIES__ASSET_PICKER, TM_TT_TYPE__CREATION_GRAPH);
    tm_the_truth_api->set_property_aspect(tt, object_type, TM_TT_PROP__CLOUD_COMPONENT__WEATHER_MAP, TM_TT_PROP_ASPECT__PROPERTIES__ASSET_PICKER, TM_TT_TYPE__CREATION_GRAPH);
}

void component_load_asset(tm_component_manager_o *manager, tm_entity_t e, tm_cloud_component_t *c, const struct tm_the_truth_o *tt, uint64_t asset)
{
    uint64_t ref;
    const tm_the_truth_object_o *object = tm_the_truth_api->read(tt, asset);

    c->quality = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY);
    c->coverage = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE);
    c->cloud_type = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__CLOUD_TYPE);
    c->precipitation = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__PRECIPITATION);
    c->wind_direction = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__WIND_DIRECTION);
    c->wind_speed = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__WIND_SPEED);
    c->planet_radius = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__PLANET_RADIUS);
    c->atmosphere_start = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_START);
    c->atmosphere_thickness = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_THICKNESS);

    // set weather map
    ref = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__WEATHER_MAP);
    if (ref != c->weather_map.asset) {
        c->weather_map.asset = ref;
        c->weather_map.version = 0;
    }

    // set volume textures
    ref = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES);
    if (ref != c->textures.asset) {
        c->textures.asset = ref;
        c->textures.version = 0;
    }
}

static void component_added(tm_component_manager_o *manager, tm_entity_t e, tm_cloud_component_t *c)
{
    c->shader = tm_shader_repository_api->lookup_shader(manager->repo, TM_STATIC_HASH("volumetric_clouds", 0xb43cfa7da8d56490ULL));
    if (c->shader) {
        tm_shader_io_o *io = tm_shader_api->shader_io(c->shader);
        tm_shader_api->create_constant_buffer_instances(io, 1, &c->cbuffer);
        tm_shader_api->create_resource_binder_instances(io, 1, &c->rbinder);
    }
}

static void component_removed(tm_component_manager_o *manager, tm_entity_t e, tm_cloud_component_t *c)
{
    if (c->shader) {
        tm_shader_io_o *io = tm_shader_api->shader_io(c->shader);
        tm_shader_api->destroy_constant_buffer_instances(io, &c->cbuffer, 1);
        tm_shader_api->destroy_resource_binder_instances(io, &c->rbinder, 1);
    }
}

void destroy_component(struct tm_component_manager_o *manager)
{
    tm_renderer_backend_i *backend = manager->rb;

    tm_entity_api->call_remove_on_all_entities(manager->ctx, tm_entity_api->lookup_component(manager->ctx, TM_TT_TYPE_HASH__CLOUD_COMPONENT));

    tm_renderer_resource_command_buffer_o *res_buf;
    backend->create_resource_command_buffers(backend->inst, &res_buf, 1);

    // destroy volumetric cloud module
    tm_render_graph_module_api->destroy(manager->module, res_buf);

    backend->submit_resource_command_buffers(backend->inst, &res_buf, 1);
    backend->destroy_resource_command_buffers(backend->inst, &res_buf, 1);

    tm_entity_context_o *ctx = manager->ctx;
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

void create_component(struct tm_entity_context_o *ctx)
{
    uint32_t num_backends;
    tm_renderer_backend_i *backend = (tm_renderer_backend_i *)(*tm_api_registry_api->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends));

    uint32_t num_repos;
    tm_shader_repository_o *repo = (tm_shader_repository_o *)(*tm_api_registry_api->implementations(TM_SHADER_REPOSITORY_INSTANCE_NAME, &num_repos));

    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__CLOUD_COMPONENT, &a);
    tm_component_manager_o *manager = tm_alloc(&a, sizeof(*manager));
    tm_the_truth_o *tt = tm_entity_api->the_truth(ctx);

    *manager = (tm_component_manager_o) {
        .ctx = ctx,
        .tt = tt,
        .rb = backend,
        .allocator = a,
        .repo = repo,
    };

    // create cloud module
    manager->module = create_volumetric_cloud_module(manager);

    tm_component_i component = {
        .name = TM_TT_TYPE__CLOUD_COMPONENT,
        .bytes = sizeof(tm_cloud_component_t),
        .manager = (tm_component_manager_o *)manager,
        .load_asset = component_load_asset,
        .add = component_added,
        .remove = component_removed,
        .destroy = destroy_component,
    };

    tm_entity_api->register_component(ctx, &component);
}

void load_cloud_component(struct tm_api_registry_api *reg, bool load)
{
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_allocator_api = reg->get(TM_ALLOCATOR_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_renderer_api = reg->get(TM_RENDERER_API_NAME);
    tm_properties_view_api = reg->get(TM_PROPERTIES_VIEW_API_NAME);
    tm_creation_graph_api = reg->get(TM_CREATION_GRAPH_API_NAME);
    tm_api_registry_api = reg->get(TM_API_REGISTRY_API_NAME);
    tm_ui_api = reg->get(TM_UI_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_shader_system_api = reg->get(TM_SHADER_SYSTEM_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_buffer_format_api = reg->get(TM_BUFFER_FORMAT_API_NAME);
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_MODULE_API_NAME);
    tm_render_graph_setup_api = reg->get(TM_RENDER_GRAPH_SETUP_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_render_graph_execute_api = reg->get(TM_RENDER_GRAPH_EXECUTE_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create_component);
}

static float properties_ui(struct tm_properties_ui_args_t *args, tm_rect_t item_rect, uint64_t object, uint32_t indent)
{
    tm_properties_float_slider_t slider01 = {
        .min = 0.0,
        .max = 1.0,
    };

    const char* options[] = {
        "Low",
        "Medium",
        "High",
        "Ultra",
        "Ground Truth",
    };

    item_rect.y = tm_properties_view_api->ui_uint32_dropdown(args, item_rect, "Quality", 0, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY, options, 0, TM_ARRAY_COUNT(options));

    bool show_weather = false;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Weather", 0, object, indent, true, &show_weather);
    if (show_weather) {
        item_rect.x += TM_PROPERTIES_METRIC_INDENT;
        item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Weather Map", 0, object, TM_TT_PROP__CLOUD_COMPONENT__WEATHER_MAP);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Coverage", 0, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, &slider01);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Precipitation", 0, object, TM_TT_PROP__CLOUD_COMPONENT__PRECIPITATION, &slider01);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Cloud Type", 0, object, TM_TT_PROP__CLOUD_COMPONENT__CLOUD_TYPE, &slider01);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Wind Direction", 0, object, TM_TT_PROP__CLOUD_COMPONENT__WIND_DIRECTION, &slider01);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Wind Speed", 0, object, TM_TT_PROP__CLOUD_COMPONENT__WIND_SPEED, &slider01);
    }
    bool show_config = false;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Configuration", 0, object, indent, false, &show_config);
    if (show_config) {
        item_rect.x += TM_PROPERTIES_METRIC_INDENT;
        item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Textures", "Base shape and detail volume textures", object, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES);
        item_rect.y = tm_properties_view_api->ui_uint32(args, item_rect, "Planet Radius", 0, object, TM_TT_PROP__CLOUD_COMPONENT__PLANET_RADIUS, 0);
        item_rect.y = tm_properties_view_api->ui_uint32(args, item_rect, "Altitude", 0, object, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_START, 0);
        item_rect.y = tm_properties_view_api->ui_uint32(args, item_rect, "Thickness", 0, object, TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_THICKNESS, 0);
    }
    return item_rect.y;
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .graph_module_inject = graph_module_inject,
    .update = update
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i) {
    .custom_ui = properties_ui,
};