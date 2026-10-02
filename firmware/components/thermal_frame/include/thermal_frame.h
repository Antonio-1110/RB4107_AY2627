#pragma once

/*
 * Heat-map frames for the dashboard. Display only: nothing here feeds the
 * safety logic, which keeps using the THERMAL_DATA summary.
 *
 * Thermal node: thermal_frame_encode() turns a 32x24 frame of °C values into
 * 1 byte per pixel, and thermal_frame_piece() cuts it into the 4
 * THERMAL_FRAME pieces that go over ESP-NOW.
 *
 * Controller: thermal_frame_assemble() collects the pieces and reports a
 * frame only once all 4 arrived. A frame with a missing piece is dropped
 * (the next one replaces it a few seconds later).
 */
#include <stdbool.h>
#include <stdint.h>
#include "rb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finest step used, 0.1 °C. Wider scenes get coarser steps so 254 levels cover them. */
#define THERMAL_FRAME_MIN_STEP_CENTI 10

/* One whole picture, 1 byte per pixel (see rb_thermal_frame_piece_t for the encoding). */
typedef struct {
    int16_t base_centi;
    uint16_t step_centi;
    uint32_t timestamp_ms;
    uint8_t pixels[RB_FRAME_PIXELS];   /* row by row, 32 per row */
} thermal_frame_t;

/*
 * Encode temps (°C, row by row). Pixels that are not finite or outside
 * [valid_min_c, valid_max_c] become RB_FRAME_PIXEL_INVALID. The range is
 * fitted to this frame: base is the coldest valid pixel and the step is the
 * smallest that still reaches the hottest one. Returns the number of valid
 * pixels.
 */
uint16_t thermal_frame_encode(const float temps[RB_FRAME_PIXELS], float valid_min_c, float valid_max_c,
                              uint32_t timestamp_ms, thermal_frame_t *out);

/* Temperature of pixel i in °C, NAN for an invalid pixel. */
float thermal_frame_pixel_c(const thermal_frame_t *frame, unsigned i);

/* Piece n (0..RB_FRAME_PIECES-1) of a frame. */
void thermal_frame_piece(const thermal_frame_t *frame, uint8_t n, rb_thermal_frame_piece_t *out);

typedef enum {
    THERMAL_FRAME_PIECE_STORED = 0,  /* waiting for the rest of the frame */
    THERMAL_FRAME_COMPLETE,          /* all pieces in: *out holds the frame */
    THERMAL_FRAME_DUPLICATE,         /* this piece was already stored */
    THERMAL_FRAME_MISMATCH,          /* disagrees with the frame's other pieces: frame discarded */
} thermal_frame_result_t;

typedef struct {
    uint32_t complete;      /* frames reassembled */
    uint32_t incomplete;    /* frames dropped because a piece never arrived */
    uint32_t mismatched;    /* frames dropped because their pieces disagreed */
} thermal_frame_stats_t;

typedef struct {
    bool active;            /* a frame is being collected */
    uint32_t node_id;
    uint32_t frame_number;  /* header sequence of THERMAL_FRAME */
    uint8_t have;           /* bit n set: piece n stored */
    thermal_frame_t frame;
    thermal_frame_stats_t stats;
} thermal_frame_assembler_t;

void thermal_frame_assembler_init(thermal_frame_assembler_t *a);

/*
 * Add one decoded piece. A piece of a different frame (or node) starts a new
 * frame and drops the unfinished one. On THERMAL_FRAME_COMPLETE the frame is
 * copied to *out and the assembler is ready for the next one.
 */
thermal_frame_result_t thermal_frame_assemble(thermal_frame_assembler_t *a, uint32_t node_id,
                                              uint32_t frame_number, const rb_thermal_frame_piece_t *piece,
                                              thermal_frame_t *out);

#ifdef __cplusplus
}
#endif
