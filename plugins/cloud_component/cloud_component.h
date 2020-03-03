#pragma once

#include <foundation/api_types.h>

struct tm_api_registry_api;

#define TM_TT_TYPE__CLOUD_COMPONENT "tm_cloud_component"
#define TM_TT_TYPE_HASH__CLOUD_COMPONENT TM_STATIC_HASH("tm_cloud_component", 0xc44dde9ce971ae04ULL)
enum {
    TM_TT_PROP__CLOUD_COMPONENT__QUALITY, // uint32
    TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, // float
    TM_TT_PROP__CLOUD_COMPONENT__BLEND_FACTOR, // float
    TM_TT_PROP__CLOUD_COMPONENT__CLOUD_TYPE, // float
    TM_TT_PROP__CLOUD_COMPONENT__EROSION, // float
    TM_TT_PROP__CLOUD_COMPONENT__TEXTURES, // reference(TM_TT_TYPE__ASSET)
    TM_TT_PROP__CLOUD_COMPONENT__WIND_DIRECTION, // float
    TM_TT_PROP__CLOUD_COMPONENT__WIND_SPEED, // float
    TM_TT_PROP__CLOUD_COMPONENT__PLANET_RADIUS, // uint32_t
    TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_START, // uint32_t
    TM_TT_PROP__CLOUD_COMPONENT__ATMOSPHERE_THICKNESS, // uint32_t
    TM_TT_PROP__CLOUD_COMPONENT__SCATTERING, // float
    TM_TT_PROP__CLOUD_COMPONENT__ABSORPTION, // float
    TM_TT_PROP__CLOUD_COMPONENT__DRAW_DISTANCE, // float
};

enum {
    TM_CLOUD_QUALITY_LOW, // 13 steps
    TM_CLOUD_QUALITY_MEDIUM, // 21 steps
    TM_CLOUD_QUALITY_HIGH, // 29 steps
    TM_CLOUD_QUALITY_ULTRA, // 37 steps
    TM_CLOUD_QUALITY_GROUND_TRUTH, // 256 steps
};

TM_DLL_EXPORT void load_cloud_component(struct tm_api_registry_api *reg, bool load);