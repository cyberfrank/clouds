#include "cloud_component.h"

#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <the_machinery/component_interfaces/shader_interface.h>

#include <plugins/default_render_pipe/default_render_pipe.h>
#include <plugins/entity/entity.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>
#include <plugins/editor_views/properties.h>
#include <plugins/creation_graph/creation_graph.h>

#include <foundation/api_registry.h>
#include <foundation/the_truth.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/allocator.h>

struct tm_logger_api *tm_logger_api;
struct tm_the_truth_api *tm_the_truth_api;
struct tm_entity_api *tm_entity_api;
struct tm_render_graph_module_api *tm_render_graph_module_api;
struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
struct tm_properties_view_api *tm_properties_view_api;
struct tm_allocator_api *tm_allocator_api;
struct tm_creation_graph_api *tm_creation_graph_api;

static tm_ci_shader_i *shader_aspect;
static tm_ci_editor_ui_i *editor_aspect;
static tm_properties_aspect_i *properties_aspect;

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
    tm_allocator_i allocator;

    tm_render_graph_module_o *module;

} tm_component_manager_o;

static void graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
}

static void update(struct tm_component_manager_o *manager, struct tm_render_graph_o *graph, struct tm_shader_system_context_o *shader_context,
    struct tm_renderer_resource_command_buffer_o *res_buf, const tm_transform_t *transforms, const uint32_t *transform_indices,
    void **component_data, uint32_t num_components,
    const uint8_t *frustum_visibility)
{
    //
}

void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "quality", TM_THE_TRUTH_PROPERTY_TYPE_UINT32_T },
        { "coverage", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        { "shape_texture", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
        { "detail_texture", TM_THE_TRUTH_PROPERTY_TYPE_REFERENCE, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CLOUD_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, object_type);

    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
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

    uint64_t tex0 = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE);
    uint64_t tex1 = tm_the_truth_api->get_reference(tt, object, TM_TT_PROP__CLOUD_COMPONENT__DETAIL_TEXTURE);

    if (tex0 && tex0 != c->shape_texture.asset)
    {
        c->shape_texture.asset = tex0;
        c->shape_texture.version = 0;
    }

    if (tex1 && tex1 != c->detail_texture.asset)
    {
        c->detail_texture.asset = tex1;
        c->detail_texture.version = 0;
    }
}

void create_component(struct tm_entity_context_o *ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__CLOUD_COMPONENT, &a);
    tm_component_manager_o *manager = tm_alloc(&a, sizeof(*manager));
    tm_the_truth_o *tt = tm_entity_api->the_truth(ctx);

    *manager = (tm_component_manager_o) {
        .ctx = ctx,
        .tt = tt,
        .allocator = a,
    };

    //manager->module = tm_render_graph_module_api->create(&a);

    tm_component_i component = {
        .name = TM_TT_TYPE__CLOUD_COMPONENT,
        .bytes = sizeof(tm_cloud_component_t),
        .manager = (tm_component_manager_o *)manager,
        .load_asset = load_asset,
    };

    tm_entity_api->register_component(ctx, &component);
}

void load_cloud_component(struct tm_api_registry_api *reg, bool load)
{
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_properties_view_api = reg->get(TM_PROPERTIES_VIEW_API_NAME);
    tm_allocator_api = reg->get(TM_ALLOCATOR_API_NAME);
    tm_creation_graph_api = reg->get(TM_CREATION_GRAPH_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create_component);
    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
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
    item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Shape Texture", 0, object, TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE);
    item_rect.y = tm_properties_view_api->ui_reference(args, item_rect, "Detail Texture", 0, object, TM_TT_PROP__CLOUD_COMPONENT__DETAIL_TEXTURE);

    return item_rect.y;
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .graph_module_inject = graph_module_inject,
    .update = update,
};

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i) {
    0
};

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i) {
    .custom_ui = properties_ui,
};