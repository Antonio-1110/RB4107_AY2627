#include "thermal_frame.h"

#include <math.h>
#include <string.h>

#define MAX_LEVEL (RB_FRAME_PIXEL_INVALID - 1u) /* 254: highest valid pixel value */
#define ALL_PIECES ((uint8_t)((1u << RB_FRAME_PIECES) - 1u))

_Static_assert(RB_FRAME_PIECES <= 8, "the assembler keeps one bit per piece in a uint8_t");

static bool usable(float t, float lo, float hi)
{
    return isfinite(t) && t >= lo && t <= hi;
}

uint16_t thermal_frame_encode(const float temps[RB_FRAME_PIXELS], float valid_min_c, float valid_max_c,
                              uint32_t timestamp_ms, thermal_frame_t *out)
{
    /* The int16 0.01 °C base can't go below -327 °C; the MLX90640 stops at -40 anyway. */
    if (valid_min_c < -300.0f) {
        valid_min_c = -300.0f;
    }
    if (valid_max_c > 320.0f) {
        valid_max_c = 320.0f;
    }
    float lo = INFINITY, hi = -INFINITY;
    uint16_t valid = 0;
    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        if (usable(temps[i], valid_min_c, valid_max_c)) {
            lo = fminf(lo, temps[i]);
            hi = fmaxf(hi, temps[i]);
            valid++;
        }
    }
    out->timestamp_ms = timestamp_ms;
    out->hot_threshold_centi = 0;
    out->hot_region_radius = 0;
    if (valid == 0) {
        out->base_centi = 0;
        out->step_centi = THERMAL_FRAME_MIN_STEP_CENTI;
        memset(out->pixels, RB_FRAME_PIXEL_INVALID, sizeof(out->pixels));
        return 0;
    }

    /* Round the base down so the coldest pixel is level 0, then fit the hottest within MAX_LEVEL. */
    const int32_t base = (int32_t)floorf(lo * 100.0f);
    const int32_t span = (int32_t)ceilf(hi * 100.0f) - base;
    int32_t step = (span + (int32_t)MAX_LEVEL - 1) / (int32_t)MAX_LEVEL;
    if (step < THERMAL_FRAME_MIN_STEP_CENTI) {
        step = THERMAL_FRAME_MIN_STEP_CENTI;
    }
    out->base_centi = (int16_t)base;
    out->step_centi = (uint16_t)step;

    for (unsigned i = 0; i < RB_FRAME_PIXELS; i++) {
        if (!usable(temps[i], valid_min_c, valid_max_c)) {
            out->pixels[i] = RB_FRAME_PIXEL_INVALID;
            continue;
        }
        float level = roundf((temps[i] * 100.0f - (float)base) / (float)step);
        level = fminf(fmaxf(level, 0.0f), (float)MAX_LEVEL);
        out->pixels[i] = (uint8_t)level;
    }
    return valid;
}

float thermal_frame_pixel_c(const thermal_frame_t *frame, unsigned i)
{
    if (i >= RB_FRAME_PIXELS || frame->pixels[i] == RB_FRAME_PIXEL_INVALID) {
        return NAN;
    }
    return ((float)frame->base_centi + (float)frame->pixels[i] * (float)frame->step_centi) / 100.0f;
}

void thermal_frame_piece(const thermal_frame_t *frame, uint8_t n, rb_thermal_frame_piece_t *out)
{
    out->piece = n;
    out->pieces = RB_FRAME_PIECES;
    out->base_centi = frame->base_centi;
    out->step_centi = frame->step_centi;
    out->timestamp_ms = frame->timestamp_ms;
    out->hot_threshold_centi = frame->hot_threshold_centi;
    out->hot_region_radius = frame->hot_region_radius;
    memcpy(out->pixels, &frame->pixels[(n % RB_FRAME_PIECES) * RB_FRAME_PIECE_PIXELS], RB_FRAME_PIECE_PIXELS);
}

void thermal_frame_assembler_init(thermal_frame_assembler_t *a)
{
    memset(a, 0, sizeof(*a));
}

static void start(thermal_frame_assembler_t *a, uint32_t node_id, uint32_t frame_number,
                  const rb_thermal_frame_piece_t *piece)
{
    if (a->active) {
        a->stats.incomplete++; /* the previous frame never got all its pieces */
    }
    a->active = true;
    a->node_id = node_id;
    a->frame_number = frame_number;
    a->have = 0;
    a->frame.base_centi = piece->base_centi;
    a->frame.step_centi = piece->step_centi;
    a->frame.timestamp_ms = piece->timestamp_ms;
    a->frame.hot_threshold_centi = piece->hot_threshold_centi;
    a->frame.hot_region_radius = piece->hot_region_radius;
}

thermal_frame_result_t thermal_frame_assemble(thermal_frame_assembler_t *a, uint32_t node_id,
                                              uint32_t frame_number, const rb_thermal_frame_piece_t *piece,
                                              thermal_frame_t *out)
{
    if (piece->piece >= RB_FRAME_PIECES) {
        return THERMAL_FRAME_MISMATCH; /* the decoder already rejects this */
    }
    if (!a->active || a->node_id != node_id || a->frame_number != frame_number) {
        start(a, node_id, frame_number, piece);
    } else if (a->frame.base_centi != piece->base_centi || a->frame.step_centi != piece->step_centi ||
               a->frame.timestamp_ms != piece->timestamp_ms) {
        a->active = false;
        a->stats.mismatched++;
        return THERMAL_FRAME_MISMATCH;
    }
    const uint8_t bit = (uint8_t)(1u << piece->piece);
    if (a->have & bit) {
        return THERMAL_FRAME_DUPLICATE;
    }
    memcpy(&a->frame.pixels[piece->piece * RB_FRAME_PIECE_PIXELS], piece->pixels, RB_FRAME_PIECE_PIXELS);
    a->have |= bit;
    if (a->have != ALL_PIECES) {
        return THERMAL_FRAME_PIECE_STORED;
    }
    *out = a->frame;
    a->active = false;
    a->stats.complete++;
    return THERMAL_FRAME_COMPLETE;
}
