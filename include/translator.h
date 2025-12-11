/*
 * Xbox 360 to DualShock 4 Input Translator
 */

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "xbox360.h"
#include "ds4.h"
#include "config.h"

/*
 * Translator configuration
 */
typedef struct {
    uint8_t stick_deadzone;         // Deadzone for analog sticks (0-127)
    uint8_t trigger_threshold;      // Digital trigger activation point (0-255)
    int     invert_left_y;          // Invert left stick Y axis
    int     invert_right_y;         // Invert right stick Y axis
    int     swap_ab;                // Swap A/B buttons (for Japanese layout)
    int     swap_xy;                // Swap X/Y buttons
} TranslatorConfig;

/*
 * Initialize translator with default configuration
 */
void translator_init(TranslatorConfig* config);

/*
 * Translate Xbox 360 report to OrbisPadData
 *
 * @param xbox      Input Xbox 360 report
 * @param ds4       Output OrbisPadData structure
 * @param config    Translator configuration (or NULL for defaults)
 */
void translator_convert(const Xbox360Report* xbox, OrbisPadData* ds4, const TranslatorConfig* config);

/*
 * Apply deadzone to stick value
 *
 * @param value     Raw stick value (0-255, center=128)
 * @param deadzone  Deadzone size (0-127)
 * @return          Adjusted value with deadzone applied
 */
uint8_t translator_apply_deadzone(uint8_t value, uint8_t deadzone);

/*
 * Simple wrapper - translate Xbox report to DS4 format using defaults
 */
void xbox360_to_ds4(const Xbox360Report* xbox, OrbisPadData* ds4);

#endif // TRANSLATOR_H
