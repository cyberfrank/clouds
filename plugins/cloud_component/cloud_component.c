#include "cloud_component.h"

#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <the_machinery/component_interfaces/shader_interface.h>
#include <the_machinery/component_interfaces/render_interface.h>

#include <plugins/entity/entity.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>

#include <foundation/api_registry.h>
#include <foundation/the_truth.h>
#include <foundation/log.h>
#include <foundation/macros.h>

struct tm_logger_api *tm_logger_api;
struct tm_the_truth_api *tm_the_truth_api;
struct tm_entity_api *tm_entity_api;
struct tm_render_graph_module_api *tm_render_graph_module_api;
struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
struct tm_render_graph_setup_api *tm_render_graph_setup_api;

static tm_ci_shader_i *shader_aspect;

void graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
    
}

static void create_truth_types(struct tm_the_truth_o *tt)
{
    tm_the_truth_property_definition_t properties[] = {
        { "coverage", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
    };

    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CLOUD_COMPONENT, properties, TM_ARRAY_COUNT(properties));
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, object_type);

    // set aspects
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_SHADER, shader_aspect);
}

void load_cloud_component(struct tm_api_registry_api *reg, bool load)
{
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_render_graph_setup_api = reg->get(TM_RENDER_GRAPH_SETUP_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i) {
    .graph_module_inject = graph_module_inject,
};