#pragma once

#include <foundation/api_types.h>

struct tm_entity_context_o;
struct tm_the_truth_o;
struct tm_allocator_i;

#define TM_TT_TYPE__SPIN_COMPONENT "tm_spin_component"
#define TM_TT_TYPE_HASH__SPIN_COMPONENT TM_STATIC_HASH("tm_spin_component", 0x6029f2980e67c3f0ULL)
enum {
    TM_TT_PROP__SPIN_COMPONENT__ANGULAR_VELOCITY, // subobject(TM_TT_TYPE__ROTATION)
};

#define TM_SPIN_COMPONENT_API_NAME "tm_spin_component_api"

struct tm_spin_component_api
{
    void (*create_truth_types)(struct tm_the_truth_o *tt);
    void (*create)(struct tm_entity_context_o *ctx);
    void (*register_engine)(struct tm_entity_context_o *ctx);
};

#if defined(TM_LINKS_SPIN_COMPONENT)
extern struct tm_spin_component_api *tm_spin_component_api;
#endif
