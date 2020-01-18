#pragma once

#include <foundation/api_types.h>

struct tm_api_registry_api;

#define TM_TT_TYPE__CLOUD_COMPONENT "tm_cloud_component"
#define TM_TT_TYPE_HASH__CLOUD_COMPONENT TM_STATIC_HASH("tm_cloud_component", 0xc44dde9ce971ae04ULL)
enum {
    TM_TT_PROP__CLOUD_COMPONENT__QUALITY, // uint32
    TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, // float
    TM_TT_PROP__CLOUD_COMPONENT__SHAPE_TEXTURE, // reference(TM_TT_TYPE__ASSET)
    TM_TT_PROP__CLOUD_COMPONENT__DETAIL_TEXTURE, // reference(TM_TT_TYPE__ASSET)
};

enum {
    TM_CLOUD_QUALITY_LOW,
    TM_CLOUD_QUALITY_MEDIUM,
    TM_CLOUD_QUALITY_HIGH,
    TM_CLOUD_QUALITY_ULTRA,
};

TM_DLL_EXPORT void load_cloud_component(struct tm_api_registry_api *reg, bool load);