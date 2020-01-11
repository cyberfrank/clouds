#include "spin_component_loader.h"

#include <foundation/api_registry.h>
#include <foundation/localizer.h>
#include <foundation/macros.h>

extern void load_actual_spin_component(struct tm_api_registry_api *reg, bool load);

static tm_localizer_strings_t localizer__get_strings(uint64_t language)
{
    typedef struct
    {
        const char *english;
        const char *context;
        const char *swedish;
    } entry_t;
    static const entry_t entries[] = {
#include "localizer_table.inl"
    };
    const char *const *strings = //
        language == TM_LANGUAGE_ENGLISH ? &entries[0].english : //
        language == TM_PSEUDO_LANGUAGE_CONTEXT ? &entries[0].context : //
            language == TM_LANGUAGE_SWEDISH ? &entries[0].swedish : //
                0;
    return (tm_localizer_strings_t){
        .num_strings = strings ? TM_ARRAY_COUNT(entries) : 0,
        .stride_bytes = sizeof(entry_t),
        .strings = strings,
    };
}

void load_spin_component(struct tm_api_registry_api *reg, bool load)
{
    tm_add_or_remove_implementation(reg, load, TM_LOCALIZER_STRINGS_INTERFACE_NAME, localizer__get_strings);
    load_actual_spin_component(reg, load);
}