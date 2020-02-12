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

static tm_ci_shader_i *shader_aspect;
static tm_ci_editor_ui_i *editor_aspect;
static tm_properties_aspect_i *properties_aspect;
struct tm_render_graph_editor_submenu_i *tm_render_graph_editor_submenu_i;

typedef struct tm_cloud_component_t {

    uint32_t quality;
    float coverage;
    float precipitation;

    // volume textures
    struct
    {
        uint64_t asset;
        uint64_t version;
    } textures;

} tm_cloud_component_t;

typedef struct tm_component_manager_o
{
    tm_entity_context_o *ctx;
    tm_the_truth_o *tt;
    tm_renderer_backend_i *rb;
    tm_allocator_i allocator;
    tm_shader_repository_o *repo;

    tm_render_graph_module_o *module;

    struct
    {
        bool visualize_weather;
    } debug_settings;

} tm_component_manager_o;

static void editor_submenu(void *inst, struct tm_ui_o *ui, const struct tm_ui_style_t *uistyle, tm_vec2_t submenu_pos)
{
    tm_component_manager_o *manager = (tm_component_manager_o *)inst;

    const tm_ui_menu_item_t items[] = {
        { .text = "Visualize Weather Texture", .toggle = &manager->debug_settings.visualize_weather },
    };

    const tm_ui_menu_t menu = {
        .pos = submenu_pos,
        .items = items,
        .num_items = TM_ARRAY_COUNT(items),
    };

    tm_ui_menu_result_t res = tm_ui_api->menu(ui, uistyle, &menu);
    (void)res;
}

#define TM_VOLUMETRIC_CLOUDS_HISTORY TM_STATIC_HASH("volumetric_clouds_history", 0xb68721c809712c0bULL)

static tm_render_graph_module_o *create_volumetric_cloud_module(tm_component_manager_o *manager)
{
    tm_render_graph_module_o *mod = tm_render_graph_module_api->create(&manager->allocator);

    tm_render_graph_toolbox_api->fullscreen_pass(mod, &(tm_fullscreen_pass_setup_t){
        .color_targets = {
            { .name = TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_DISCARD },
        },
        .depth_stencil_target = {
            .name = TM_DEFAULT_RENDER_PIPE_MAIN_DEPTH_STENCIL_TARGET, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_LOAD
        },
        .input_slots = {
            { .slot_name = TM_STATIC_HASH("history", 0xee0b17af3674bb0fULL), .resources = { { TM_VOLUMETRIC_CLOUDS_HISTORY } } },
        },
        .shader = TM_STATIC_HASH("volumetric_clouds", 0xb43cfa7da8d56490ULL),
    }, "Volumetric Clouds");

    tm_render_graph_toolbox_api->fullscreen_pass(mod, &(tm_fullscreen_pass_setup_t){
        .color_targets = {
            { .name = TM_VOLUMETRIC_CLOUDS_HISTORY, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_DISCARD } },
        .shader = TM_STATIC_HASH("copy", 0xd996b7bdf040ea8bULL),
        .output_pass = true,
        .input_slots = { { .slot_name = TM_STATIC_HASH("textures", 0x64e318ce6361e6acULL), .resources = { { TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION } } } }
    }, "Volumetric Clouds Copy");

    tm_render_graph_module_api->set_editor_submenu_interface(mod, &(tm_render_graph_module_editor_submenu_i){
        .submenu_name = "Cloud Module",
        .inst = manager,
        .submenu = editor_submenu,
    });

    return mod;
}

static void graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
    tm_render_graph_module_api->create_persistent_gpu_image(module, TM_VOLUMETRIC_CLOUDS_HISTORY, 0, TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION);
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
    TM_INIT_TEMP_ALLOCATOR(ta);

    tm_shader_o *shader = tm_shader_repository_api->lookup_shader(manager->repo, TM_STATIC_HASH("volumetric_clouds", 0xb43cfa7da8d56490ULL));

    if (!shader)
        return;

    tm_shader_io_o *io = tm_shader_api->shader_io(shader);
    /* carray */ tm_shader_constant_buffer_instance_t *cbuffer = 0;
    /* carray */ tm_shader_resource_binder_instance_t *rbinder = 0;
    tm_carray_temp_resize(cbuffer, num_components, ta);
    tm_carray_temp_resize(rbinder, num_components, ta);
    tm_shader_api->create_constant_buffer_instances(io, num_components, cbuffer);
    tm_shader_api->create_resource_binder_instances(io, num_components, rbinder);

    tm_cloud_component_t **cdata = (tm_cloud_component_t **)component_data;

    for (uint32_t i = 0; i < num_components; ++i) {
        tm_cloud_component_t *c = cdata[i];

        uint32_t num_steps = (c->quality + 1) << 3;
        set_constant(io, res_buf, &cbuffer[i], TM_STATIC_HASH("num_steps", 0x55f5f64778324518ULL), &num_steps, sizeof(uint32_t));

        uint64_t wanted_version = tm_creation_graph_api->graph_version(c->textures.asset);
        if (wanted_version != c->textures.version) {
            tm_creation_graph_output_t image_output = tm_creation_graph_api->output(manager->tt, c->textures.asset, TM_CREATION_GRAPH__IMAGE__OUTPUT_NODE_HASH, wanted_version, manager->rb, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, 0, 0);
            c->textures.version = image_output.version;

            tm_creation_graph_image_data_t *image = (tm_creation_graph_image_data_t *)image_output.output;
            TM_LOG("Loaded %i creation graph resources.\n", image_output.num_output_objects);  
            if (set_resource(io, res_buf, &rbinder[i], TM_STATIC_HASH("test", 0x2f4a8724618f4c63ULL), &image->handle, 0, 0, 1)) {
                // 
            }
        }
    }

    tm_shader_api->destroy_constant_buffer_instances(io, cbuffer, num_components);
    tm_shader_api->destroy_resource_binder_instances(io, rbinder, num_components);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "quality", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "coverage", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "precipitation", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "textures", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CLOUD_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, object_type);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);
    tm_the_truth_api->set_property_aspect(tt, object_type, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES, TM_TT_PROP_ASPECT__PROPERTIES__ASSET_PICKER, TM_TT_TYPE__CREATION_GRAPH);
}

void load_asset(tm_component_manager_o *manager, tm_entity_t e, void *data, const struct tm_the_truth_o *tt, uint64_t asset)
{
    tm_cloud_component_t *c = data;
    const tm_the_truth_object_o *object = tm_the_truth_api->read(tt, asset);

    c->quality = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY);
    c->coverage = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE);
    c->precipitation = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__PRECIPITATION);

    uint64_t ref = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES);
    if (ref && ref != c->textures.asset) {
        c->textures.asset = ref;
        c->textures.version = 0;
    }
}

void destroy_component(struct tm_component_manager_o *manager)
{
    tm_renderer_backend_i *backend = manager->rb;

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

    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__CLOUD_COMPONENT, &a);
    tm_component_manager_o *manager = tm_alloc(&a, sizeof(*manager));
    tm_the_truth_o *tt = tm_entity_api->the_truth(ctx);

    uint32_t num_repos;
    tm_shader_repository_o *repo = (tm_shader_repository_o *)(*tm_api_registry_api->implementations(TM_SHADER_REPOSITORY_INSTANCE_NAME, &num_repos));

    *manager = (tm_component_manager_o) {
        .ctx = ctx,
        .tt = tt,
        .rb = backend,
        .allocator = a,
        .repo = repo,
    };

    // 
    tm_creation_graph_api->create_truth_types(manager->tt);

    // create cloud module
    manager->module = create_volumetric_cloud_module(manager);

    tm_component_i component = {
        .name = TM_TT_TYPE__CLOUD_COMPONENT,
        .bytes = sizeof(tm_cloud_component_t),
        .manager = (tm_component_manager_o *)manager,
        .load_asset = load_asset,
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
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_MODULE_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_properties_view_api = reg->get(TM_PROPERTIES_VIEW_API_NAME);
    tm_creation_graph_api = reg->get(TM_CREATION_GRAPH_API_NAME);
    tm_api_registry_api = reg->get(TM_API_REGISTRY_API_NAME);
    tm_ui_api = reg->get(TM_UI_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_shader_system_api = reg->get(TM_SHADER_SYSTEM_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_buffer_format_api = reg->get(TM_BUFFER_FORMAT_API_NAME);

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
    };

    item_rect.y = tm_properties_view_api->ui_uint32_dropdown(args, item_rect, "Quality", 0, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY, options, 0, TM_ARRAY_COUNT(options));

    bool show_weather = false;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Weather", 0, object, indent, true, &show_weather);
    if (show_weather) {
        item_rect.x += TM_PROPERTIES_METRIC_INDENT;
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Coverage", 0, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, &slider01);
        item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Precipitation", 0, object, TM_TT_PROP__CLOUD_COMPONENT__PRECIPITATION, &slider01);
    }
    bool show_config = false;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Configuration", 0, object, indent, false, &show_config);
    if (show_config) {
        item_rect.x += TM_PROPERTIES_METRIC_INDENT;
        item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Textures", "Base shape and detail volume textures", object, TM_TT_PROP__CLOUD_COMPONENT__TEXTURES);
    }
    
    return item_rect.y;
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .graph_module_inject = graph_module_inject,
    // .update = update
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i) {
    .custom_ui = properties_ui,
};