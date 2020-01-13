#pragma once

#include <foundation/api_types.h>

struct tm_api_registry_api;

#define TM_TT_TYPE__CLOUD_COMPONENT "tm_cloud_component"
#define TM_TT_TYPE_HASH__CLOUD_COMPONENT TM_STATIC_HASH("tm_cloud_component", 0xc44dde9ce971ae04ULL)
enum {
    TM_TT_PROP__CLOUD_COMPONENT__COVERAGE, // float
};

TM_DLL_EXPORT void load_cloud_component(struct tm_api_registry_api *reg, bool load);