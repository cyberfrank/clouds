#include "cloud_component.h"

#include <foundation/api_registry.h>
#include <foundation/the_truth.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/api_registry.h>
#include <foundation/allocator.h>
#include <foundation/temp_allocator.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/buffer_format.h>
#include <foundation/carray.inl>

#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
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
#include <the_machinery/component_interfaces/render_interface.h>
#include <the_machinery/render_context.h>

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

static tm_ci_render_i *render_aspect;
static tm_ci_editor_ui_i *editor_aspect;
static tm_properties_aspect_i *properties_aspect;
struct tm_render_graph_editor_submenu_i *tm_render_graph_editor_submenu_i;

typedef struct tm_cloud_component_t {

    uint32_t quality;
    float coverage;
    struct
    {
        uint64_t asset;
        uint64_t version;
    } shape_texture, detail_texture;
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

/*
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

static tm_render_graph_module_o *create_volumetric_cloud_module(tm_component_manager_o *manager)
{
    tm_render_graph_module_o *mod = tm_render_graph_module_api->create(&manager->allocator);

    tm_render_graph_toolbox_api->fullscreen_pass(mod, &(tm_fullscreen_pass_setup_t) {
        .color_targets = {
            { .name = TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_LOAD }
        },
        .depth_stencil_target = {
            .name = TM_DEFAULT_RENDER_PIPE_MAIN_DEPTH_STENCIL_TARGET, .load_op = TM_RENDERER_RESOURCE_LOAD_OP_LOAD
        },
        .shader = TM_STATIC_HASH("volumetric_clouds", 0xb43cfa7da8d56490ULL)
    }, "Volumetric Clouds");

    tm_render_graph_module_api->set_editor_submenu_interface(mod, &(tm_render_graph_module_editor_submenu_i){
        .submenu_name = "Cloud Module",
        .inst = manager,
        .submenu = editor_submenu,
    });

    return mod;
}

static void graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
    tm_render_graph_module_api->insert_extension(module, TM_DEFAULT_RENDER_PIPE_MAIN_EXTENSION_SKYDOME, manager->module);
}
*/

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

static bool set_resource(tm_shader_io_o *io, tm_renderer_resource_command_buffer_o *res_buf, tm_shader_resource_binder_instance_t *instance,
    uint64_t name, const uint32_t *resource_handle, const uint32_t *aspect_flags, uint32_t first_resource, uint32_t n_resources)
{
    tm_shader_resource_t resource;
    uint32_t resource_slot;
    if (tm_shader_api->lookup_resource(io, name, &resource, &resource_slot)) {
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t) {
            .instance_id = instance->instance_id,
            .resource_slot = resource_slot,
            .first_resource = first_resource,
            .num_resources = n_resources,
            .resources = resource_handle,
            .resources_view_aspect_flags = aspect_flags,
        }, 1);
        return true;
    }
    return false;
}

static void render(struct tm_component_manager_o *manager, struct tm_render_context_args_t *args, const tm_ci_render_viewer_t *viewers,
    uint32_t num_viewers, const tm_entity_t *entities, const tm_transform_component_t *entity_transforms, const bool *entity_selection_state, const uint32_t *entity_indices,
    void **render_component_data, uint32_t num_renderables, const uint8_t *frustum_visibility)
{
    TM_INIT_TEMP_ALLOCATOR_WITH_ADAPTER(ta, a);

    tm_shader_o *shader = tm_shader_repository_api->lookup_shader(args->shader_repository, TM_STATIC_HASH("volumetric_clouds", 0xb43cfa7da8d56490ULL));
    
    if (!shader)
        return;

    tm_render_pipeline_shader_system_t selection_system;
    args->render_pipeline->api->global_shader_system(args->render_pipeline->inst, TM_RENDER_PIPELINE_EDITOR_SELECTION, &selection_system);
    tm_shader_system_context_o *shader_context = tm_shader_system_api->clone_context(args->shader_context, a);

    tm_shader_io_o *io = tm_shader_api->shader_io(shader);

    /* carray */ tm_shader_constant_buffer_instance_t *cbuffer = 0;
    /* carray */ tm_shader_resource_binder_instance_t *rbinder = 0;
    tm_carray_temp_resize(cbuffer, num_renderables, ta);
    tm_carray_temp_resize(rbinder, num_renderables, ta);
    tm_shader_api->create_constant_buffer_instances(io, num_renderables, cbuffer);
    tm_shader_api->create_resource_binder_instances(io, num_renderables, rbinder);

    /* carray */ tm_renderer_shader_info_t *shader_infos = 0;
    tm_carray_temp_resize(shader_infos, num_viewers * num_renderables, ta);

    /* carray */ tm_renderer_draw_call_info_t *draw_calls = 0;
    tm_carray_temp_resize(draw_calls, num_viewers * num_renderables, ta);

    /* carray */ uint64_t *sort_keys = 0;
    tm_carray_temp_resize(sort_keys, num_viewers * num_renderables, ta);

    tm_cloud_component_t **cdata = (tm_cloud_component_t **)render_component_data;

    uint32_t num_draws = 0;
    for (uint32_t i = 0; i < num_renderables; ++i) {
        tm_cloud_component_t *c = cdata[i];

        bool updated = false;
        for (uint32_t v = 0; v < num_viewers; ++v) {
            const tm_ci_render_viewer_t viewer = viewers[v];
            tm_shader_system_api->activate_system(shader_context, viewer.viewer_system, viewer.viewer_cbuffer, 1, viewer.viewer_rbinder, viewer.viewer_rbinder ? 1 : 0);

            if (!updated) {
                set_constant(io, args->default_resource_buffer, &cbuffer[i], TM_STATIC_HASH("coverage", 0x6dde46384338bcacULL), &c->coverage, sizeof(float));
                updated = true;
            }

            tm_shader_api->assemble_shader_infos(shader, 0, 0, shader_context, args->default_resource_buffer, &cbuffer[i], &rbinder[i], 1, &shader_infos[num_draws]);

            draw_calls[num_draws] = (tm_renderer_draw_call_info_t) {
                .primitive_type = TM_RENDERER_PRIMITIVE_TYPE_TRIANGLE_LIST,
                .draw_type = TM_RENDERER_DRAW_TYPE_NON_INDEXED,
                .non_indexed = {
                    .num_vertices = 3,
                    .first_vertex = 0,
                },
                .num_instances = 1,
            };
            sort_keys[num_draws] = viewer.sort_key;
            ++num_draws;

            tm_shader_system_api->deactivate_system(shader_context, viewer.viewer_system);
        }
    }

    //tm_renderer_api->tm_renderer_command_buffer_api->draw_calls(args->default_command_buffer, sort_keys, draw_calls, shader_infos, num_draws);

    tm_shader_api->destroy_constant_buffer_instances(io, cbuffer, num_renderables);
    tm_shader_api->destroy_resource_binder_instances(io, rbinder, num_renderables);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "quality", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "coverage", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "cloud_shape", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
        { "cloud_detail", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CLOUD_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, object_type);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_RENDER, render_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, object_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);
    tm_the_truth_api->set_property_aspect(tt, object_type, TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE, TM_TT_PROP_ASPECT__PROPERTIES__ASSET_PICKER, TM_TT_TYPE__CREATION_GRAPH);
    tm_the_truth_api->set_property_aspect(tt, object_type, TM_TT_PROP__CLOUD_COMPONENT__DETAIL_TEXTURE, TM_TT_PROP_ASPECT__PROPERTIES__ASSET_PICKER, TM_TT_TYPE__CREATION_GRAPH);
}

void load_asset(tm_component_manager_o *manager, tm_entity_t e, void *data, const struct tm_the_truth_o *tt, uint64_t asset)
{
    tm_cloud_component_t *c = data;
    const tm_the_truth_object_o *object = tm_the_truth_api->read(tt, asset);

    c->quality = tm_the_truth_api->get_uint32_t(tt, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY);
    c->coverage = tm_the_truth_api->get_float(tt, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE);

    uint64_t ref = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE);
    if (ref && ref != c->shape_texture.asset) {
        c->shape_texture.asset = ref;
        c->shape_texture.version = 0;
    }
}

void destroy_component(struct tm_component_manager_o *manager)
{
    tm_renderer_backend_i *backend = manager->rb;

    tm_renderer_resource_command_buffer_o *res_buf;
    backend->create_resource_command_buffers(backend->inst, &res_buf, 1);

    // destroy volumetric cloud module
    // tm_render_graph_module_api->destroy(manager->module, res_buf);

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

    // create cloud module
    // manager->module = create_volumetric_cloud_module(manager);

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
    tm_properties_float_slider_t slider = {
        .min = 0,
        .max = 1,
    };

    const char* options[] = {
        "Low",
        "Medium",
        "High",
        "Ultra",
    };

    item_rect.y = tm_properties_view_api->ui_float_slider(args, item_rect, "Coverage", 0, object, TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, &slider);
    item_rect.y = tm_properties_view_api->ui_uint32_dropdown(args, item_rect, "Quality", 0, object, TM_TT_PROP__CLOUD_COMPONENT__QUALITY, options, 0, TM_ARRAY_COUNT(options));
    item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Cloud Shape", 0, object, TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE);
    item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Cloud Detail", 0, object, TM_TT_PROP__CLOUD_COMPONENT__DETAIL_TEXTURE);
    
    return item_rect.y;
}

/*
static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .graph_module_inject = graph_module_inject,
    .update = update
};
*/

static tm_ci_render_i *render_aspect = &(tm_ci_render_i) {
    .render = render,
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i) {
    .custom_ui = properties_ui,
};