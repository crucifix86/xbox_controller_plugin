/*
 * Xbox 360 USB Controller Reader
 * Handles USB enumeration, connection, and input polling
 */

#ifndef USB_XBOX_H
#define USB_XBOX_H

#include "xbox360.h"
#include "config.h"
#include <stdint.h>

/*
 * Controller connection state
 */
typedef enum {
    XBOX_STATE_DISCONNECTED = 0,
    XBOX_STATE_CONNECTED,
    XBOX_STATE_ERROR
} XboxControllerState;

/*
 * Controller slot information
 */
typedef struct {
    XboxControllerState state;
    Xbox360Report       last_report;
    uint64_t            last_update;    // Timestamp of last report
    uint16_t            vendor_id;
    uint16_t            product_id;
} XboxControllerSlot;

/*
 * Initialize USB subsystem and start controller detection
 * @return 0 on success, negative on error
 */
int xbox_usb_init(void);

/*
 * Cleanup USB subsystem
 */
void xbox_usb_cleanup(void);

/*
 * Start background polling thread
 * @return 0 on success, negative on error
 */
int xbox_usb_start_polling(void);

/*
 * Stop background polling thread
 */
void xbox_usb_stop_polling(void);

/*
 * Get number of connected Xbox controllers
 * @return Number of connected controllers (0-4)
 */
int xbox_usb_get_controller_count(void);

/*
 * Check if a specific controller slot is connected
 * @param index Controller index (0-3)
 * @return 1 if connected, 0 if not
 */
int xbox_usb_is_connected(int index);

/*
 * Read latest report from controller
 * @param index     Controller index (0-3)
 * @param report    Output report structure
 * @return 0 on success, negative on error or no data
 */
int xbox_usb_read_report(int index, Xbox360Report* report);

/*
 * Send rumble command to controller
 * @param index         Controller index (0-3)
 * @param left_motor    Left (large) motor intensity (0-255)
 * @param right_motor   Right (small) motor intensity (0-255)
 * @return 0 on success, negative on error
 */
int xbox_usb_set_rumble(int index, uint8_t left_motor, uint8_t right_motor);

/*
 * Get controller slot information
 * @param index Controller index (0-3)
 * @return Pointer to slot info, or NULL if invalid index
 */
const XboxControllerSlot* xbox_usb_get_slot(int index);

/*
 * Force rescan for controllers
 * Useful after USB device changes
 */
void xbox_usb_rescan(void);

#endif // USB_XBOX_H
