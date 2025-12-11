/*
 * Xbox 360 USB Controller Reader Implementation
 *
 * Uses PS4's sceUsbd library (libusb wrapper) to communicate
 * with Xbox 360 controllers connected via USB.
 */

#include "usb_xbox.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

// OpenOrbis headers
#include <orbis/Usbd.h>
#include <orbis/libkernel.h>

// pthread from OpenOrbis
#include <pthread.h>
#include <unistd.h>

// Debug notification helper
static void usb_notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/*
 * Internal controller state
 */
typedef struct {
    XboxControllerSlot  slot;
    libusb_device_handle* handle;
    int                 interface_claimed;
    pthread_mutex_t     mutex;
} InternalController;

// Global state
static InternalController g_controllers[MAX_XBOX_CONTROLLERS];
static pthread_t          g_poll_thread;
static volatile int       g_polling_active = 0;
static volatile int       g_initialized = 0;

/*
 * Check if device is an Xbox 360 controller
 */
static int is_xbox360_controller(uint16_t vid, uint16_t pid) {
    if (vid != XBOX360_VID) {
        return 0;
    }

    switch (pid) {
        case XBOX360_PID_WIRED:
        case XBOX360_PID_WIRELESS:
            return 1;
        default:
            return 0;
    }
}

/*
 * Open and configure a controller
 */
static int open_controller(libusb_device* dev, int slot_index) {
    InternalController* ctrl = &g_controllers[slot_index];
    int ret;

    // Open device
    ret = sceUsbdOpen(dev, &ctrl->handle);
    if (ret < 0 || ctrl->handle == NULL) {
        return -1;
    }

    // Claim interface 0 (main controller interface)
    ret = sceUsbdClaimInterface(ctrl->handle, 0);
    if (ret < 0) {
        sceUsbdClose(ctrl->handle);
        ctrl->handle = NULL;
        return -2;
    }

    ctrl->interface_claimed = 1;
    ctrl->slot.state = XBOX_STATE_CONNECTED;

    return 0;
}

/*
 * Close a controller
 */
static void close_controller(int slot_index) {
    InternalController* ctrl = &g_controllers[slot_index];

    pthread_mutex_lock(&ctrl->mutex);

    if (ctrl->interface_claimed) {
        sceUsbdReleaseInterface(ctrl->handle, 0);
        ctrl->interface_claimed = 0;
    }

    if (ctrl->handle) {
        sceUsbdClose(ctrl->handle);
        ctrl->handle = NULL;
    }

    ctrl->slot.state = XBOX_STATE_DISCONNECTED;
    memset(&ctrl->slot.last_report, 0, sizeof(Xbox360Report));

    pthread_mutex_unlock(&ctrl->mutex);
}

/*
 * Scan for Xbox controllers
 */
static void scan_controllers(void) {
    libusb_device** device_list = NULL;
    int32_t device_count;
    int slot = 0;

    // Get list of USB devices
    device_count = sceUsbdGetDeviceList(&device_list);
    if (device_count < 0 || device_list == NULL) {
        return;
    }

    // Find first available slot
    for (slot = 0; slot < MAX_XBOX_CONTROLLERS; slot++) {
        if (g_controllers[slot].slot.state == XBOX_STATE_DISCONNECTED) {
            break;
        }
    }

    // Scan devices
    for (int32_t i = 0; i < device_count && slot < MAX_XBOX_CONTROLLERS; i++) {
        struct libusb_device_descriptor desc;

        if (sceUsbdGetDeviceDescriptor(device_list[i], &desc) != 0) {
            continue;
        }

        if (is_xbox360_controller(desc.idVendor, desc.idProduct)) {
            // Check if this device is already opened
            int already_opened = 0;
            for (int j = 0; j < MAX_XBOX_CONTROLLERS; j++) {
                if (g_controllers[j].slot.state == XBOX_STATE_CONNECTED &&
                    g_controllers[j].slot.vendor_id == desc.idVendor &&
                    g_controllers[j].slot.product_id == desc.idProduct) {
                    already_opened = 1;
                    break;
                }
            }

            if (!already_opened) {
                if (open_controller(device_list[i], slot) == 0) {
                    g_controllers[slot].slot.vendor_id = desc.idVendor;
                    g_controllers[slot].slot.product_id = desc.idProduct;

                    // Find next available slot
                    for (slot++; slot < MAX_XBOX_CONTROLLERS; slot++) {
                        if (g_controllers[slot].slot.state == XBOX_STATE_DISCONNECTED) {
                            break;
                        }
                    }
                }
            }
        }
    }

    sceUsbdFreeDeviceList(device_list);
}

/*
 * Read input from a single controller
 */
static int read_controller_input(int slot_index) {
    InternalController* ctrl = &g_controllers[slot_index];
    Xbox360Report report;
    int32_t transferred = 0;
    int32_t ret;

    if (ctrl->slot.state != XBOX_STATE_CONNECTED || ctrl->handle == NULL) {
        return -1;
    }

    // Read from interrupt endpoint
    ret = sceUsbdInterruptTransfer(
        ctrl->handle,
        XBOX360_ENDPOINT_IN,
        (unsigned char*)&report,
        sizeof(Xbox360Report),
        &transferred,
        USB_TRANSFER_TIMEOUT_MS
    );

    if (ret == 0 && transferred == sizeof(Xbox360Report)) {
        // Validate report
        if (xbox360_report_valid(&report)) {
            pthread_mutex_lock(&ctrl->mutex);
            ctrl->slot.last_report = report;
            ctrl->slot.last_update = sceKernelGetProcessTime();
            pthread_mutex_unlock(&ctrl->mutex);
            return 0;
        }
    } else if (ret < 0) {
        // Check if controller disconnected
        if (sceUsbdCheckConnected(ctrl->handle) != 0) {
            close_controller(slot_index);
            return -2;
        }
    }

    return -1;
}

/*
 * Polling thread function
 */
static void* poll_thread_func(void* arg) {
    (void)arg;
    int scan_counter = 0;

    while (g_polling_active) {
        // Periodically scan for new controllers
        if (++scan_counter >= 250) {  // Every ~1 second at 4ms interval
            scan_controllers();
            scan_counter = 0;
        }

        // Read from all connected controllers
        for (int i = 0; i < MAX_XBOX_CONTROLLERS; i++) {
            if (g_controllers[i].slot.state == XBOX_STATE_CONNECTED) {
                read_controller_input(i);
            }
        }

        // Sleep for poll interval
        sceKernelUsleep(USB_POLL_INTERVAL_US);
    }

    return NULL;
}

/*
 * Public API Implementation
 */

int xbox_usb_init(void) {
    if (g_initialized) {
        return 0;
    }

    usb_notify("USB: Calling sceUsbdInit...");

    // Initialize libusb via PS4 wrapper
    int32_t ret = sceUsbdInit();
    if (ret < 0) {
        usb_notify("USB: sceUsbdInit failed");
        return -1;
    }

    usb_notify("USB: Init OK, setting up slots...");

    // Initialize controller slots
    for (int i = 0; i < MAX_XBOX_CONTROLLERS; i++) {
        memset(&g_controllers[i], 0, sizeof(InternalController));
        g_controllers[i].slot.state = XBOX_STATE_DISCONNECTED;
        pthread_mutex_init(&g_controllers[i].mutex, NULL);
    }

    usb_notify("USB: Slots OK, scanning...");

    g_initialized = 1;

    // Do initial scan
    scan_controllers();

    usb_notify("USB: Scan complete");

    return 0;
}

void xbox_usb_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    // Stop polling if active
    xbox_usb_stop_polling();

    // Close all controllers
    for (int i = 0; i < MAX_XBOX_CONTROLLERS; i++) {
        close_controller(i);
        pthread_mutex_destroy(&g_controllers[i].mutex);
    }

    // Cleanup libusb
    sceUsbdExit();

    g_initialized = 0;
}

int xbox_usb_start_polling(void) {
    if (!g_initialized || g_polling_active) {
        return -1;
    }

    g_polling_active = 1;

    int ret = pthread_create(&g_poll_thread, NULL, poll_thread_func, NULL);
    if (ret != 0) {
        g_polling_active = 0;
        return -2;
    }

    return 0;
}

void xbox_usb_stop_polling(void) {
    if (!g_polling_active) {
        return;
    }

    g_polling_active = 0;
    pthread_join(g_poll_thread, NULL);
}

int xbox_usb_get_controller_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_XBOX_CONTROLLERS; i++) {
        if (g_controllers[i].slot.state == XBOX_STATE_CONNECTED) {
            count++;
        }
    }
    return count;
}

int xbox_usb_is_connected(int index) {
    if (index < 0 || index >= MAX_XBOX_CONTROLLERS) {
        return 0;
    }
    return g_controllers[index].slot.state == XBOX_STATE_CONNECTED;
}

int xbox_usb_read_report(int index, Xbox360Report* report) {
    if (index < 0 || index >= MAX_XBOX_CONTROLLERS || report == NULL) {
        return -1;
    }

    InternalController* ctrl = &g_controllers[index];

    if (ctrl->slot.state != XBOX_STATE_CONNECTED) {
        return -2;
    }

    pthread_mutex_lock(&ctrl->mutex);
    *report = ctrl->slot.last_report;
    pthread_mutex_unlock(&ctrl->mutex);

    return 0;
}

int xbox_usb_set_rumble(int index, uint8_t left_motor, uint8_t right_motor) {
    if (index < 0 || index >= MAX_XBOX_CONTROLLERS) {
        return -1;
    }

    InternalController* ctrl = &g_controllers[index];

    if (ctrl->slot.state != XBOX_STATE_CONNECTED || ctrl->handle == NULL) {
        return -2;
    }

    // Prepare rumble output report
    Xbox360OutputReport out;
    xbox360_init_rumble(&out, left_motor, right_motor);

    int32_t transferred = 0;
    int32_t ret = sceUsbdInterruptTransfer(
        ctrl->handle,
        XBOX360_ENDPOINT_OUT,
        (unsigned char*)&out,
        sizeof(Xbox360OutputReport),
        &transferred,
        USB_TRANSFER_TIMEOUT_MS
    );

    return (ret == 0) ? 0 : -3;
}

const XboxControllerSlot* xbox_usb_get_slot(int index) {
    if (index < 0 || index >= MAX_XBOX_CONTROLLERS) {
        return NULL;
    }
    return &g_controllers[index].slot;
}

void xbox_usb_rescan(void) {
    scan_controllers();
}
