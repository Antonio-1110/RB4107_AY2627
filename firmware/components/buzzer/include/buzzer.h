#pragma once

/*
 * Buzzer driver (TODO section 13).
 *
 * Patterns are played by an esp_timer, so no caller ever blocks or sleeps:
 * buzzer_set_pattern() just switches what the timer plays.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUZZER_PATTERN_OFF = 0,
    BUZZER_PATTERN_ON,         /* continuous */
    BUZZER_PATTERN_WARNING,    /* 200 ms on / 800 ms off */
    BUZZER_PATTERN_SHUTDOWN,   /* 150 ms on / 150 ms off */
    BUZZER_PATTERN_FAULT,      /* double chirp every 2 s */
    BUZZER_PATTERN_CHIRP,      /* one 100 ms beep, then off */
    BUZZER_PATTERN_COUNT,
} buzzer_pattern_t;

typedef struct {
    int gpio;                  /* -1: no buzzer, all calls become no-ops */
    int active_level;
    bool pwm;                  /* passive buzzer: drive a tone with LEDC */
    uint32_t tone_hz;
} buzzer_config_t;

/* Buzzer settings from menuconfig. */
buzzer_config_t buzzer_config_from_kconfig(void);

/* Configure the pin and leave the buzzer silent. */
esp_err_t buzzer_init(const buzzer_config_t *config);

void buzzer_on(void);
void buzzer_off(void);
void buzzer_set_pattern(buzzer_pattern_t pattern);
buzzer_pattern_t buzzer_get_pattern(void);
const char *buzzer_pattern_name(buzzer_pattern_t pattern);

#ifdef __cplusplus
}
#endif
