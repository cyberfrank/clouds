#pragma once

#include <foundation/api_types.h>

struct tm_api_registry_api;

#define TM_TT_TYPE__SKY_COMPONENT "tm_sky_component"
#define TM_TT_TYPE_HASH__SKY_COMPONENT TM_STATIC_HASH("tm_sky_component", 0x91f11c01d8cef22cULL);
enum {
    TM_TT_PROP__SKY_COMPONENT__BETA_R, // tm_vec3_t
    TM_TT_PROP__SKY_COMPONENT__BETA_M, // tm_vec3_t
    TM_TT_PROP__SKY_COMPONENT__MIE_G, // float
    TM_TT_PROP__SKY_COMPONENT__MIE_C, // float
    TM_TT_PROP__SKY_COMPONENT__RAY_C, // float
    TM_TT_PROP__SKY_COMPONENT__TURBIDITY, // float
    TM_TT_PROP__SKY_COMPONENT__SUN_SIZE, // float
    TM_TT_PROP__SKY_COMPONENT__SUN_INTENSITY, // float
};

TM_DLL_EXPORT void load_sky_component(struct tm_api_registry_api *reg, bool load);