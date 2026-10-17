#include "rb_params.h"

#include <math.h>
#include <string.h>

const rb_param_info_t RB_PARAMS[RB_PARAM_COUNT] = {
#define RB_PARAM(key, kind, min, max, source, options, label, get, set) [RB_PARAM_##key] = {#key, min, max},
#include "rb_params.def"
#undef RB_PARAM
};

int rb_params_find(const char *key)
{
    for (int i = 0; i < RB_PARAM_COUNT; i++) {
        if (strcmp(key, RB_PARAMS[i].key) == 0) {
            return i;
        }
    }
    return -1;
}

void rb_params_read(const rb_params_target_t *t, rb_params_t *out)
{
#define RB_PARAM(key, kind, min, max, source, options, label, get, set) out->v[RB_PARAM_##key] = (uint32_t)(get);
#include "rb_params.def"
#undef RB_PARAM
}

bool rb_params_in_range(const rb_params_t *p)
{
    for (int i = 0; i < RB_PARAM_COUNT; i++) {
        if (p->v[i] < RB_PARAMS[i].min || p->v[i] > RB_PARAMS[i].max) {
            return false;
        }
    }
    return true;
}

void rb_params_write(const rb_params_t *p, rb_params_target_t *t)
{
#define RB_PARAM(key, kind, min, max, source, options, label, get, set) \
    {                                                                   \
        const uint32_t v = p->v[RB_PARAM_##key];                        \
        set;                                                            \
    }
#include "rb_params.def"
#undef RB_PARAM
}

uint32_t rb_params_layout_hash(void)
{
    uint32_t h = 2166136261u; /* FNV-1a over the keys, each with its terminator */
    for (int i = 0; i < RB_PARAM_COUNT; i++) {
        for (const char *c = RB_PARAMS[i].key;; c++) {
            h = (h ^ (uint8_t)*c) * 16777619u;
            if (*c == '\0') {
                break;
            }
        }
    }
    return h;
}

bool rb_params_target_valid(const rb_params_target_t *t)
{
    return t->health.stale_timeout_ms > 0 && t->health.stale_timeout_ms < t->health.offline_timeout_ms;
}
