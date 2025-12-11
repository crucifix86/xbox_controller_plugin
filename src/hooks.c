/*
 * Hybrid approach: Hook scePad like gamepad_helper does
 * Read Xbox controller via USB thread, inject into scePadReadState
 */

#include "hooks.h"
#include "config.h"
#include "translator.h"
#include "xbox360.h"
#include "usb_xbox.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/Usbd.h>

#include <GoldHEN.h>
#include <Detour.h>
#include <Patcher.h>
#include <Utilities.h>

// External function declarations
extern int32_t scePadReadExt(int32_t handle, OrbisPadData* pData, int32_t num);
extern int32_t scePadReadStateExt(int32_t handle, OrbisPadData* pData);
extern int sys_dynlib_load_prx(const char* path, int* handle);
extern const char* sceKernelGetFsSandboxRandomWord(void);

// Hooks for both read functions
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);

// Patchers
static Patcher* g_padReadExtPatcher = NULL;
static Patcher* g_padReadStateExtPatcher = NULL;

static int g_hooks_installed = 0;
static int g_xbox_active = 0;

static void hook_notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Try to read Xbox controller via /dev/uhid* file descriptor
static int g_uhid_fd = -1;
static int g_uhid_tried = 0;

// Forward declare usb init function
static int try_usb_init(void);

static void try_open_uhid(void) {
    if (g_uhid_tried) return;
    g_uhid_tried = 1;
    try_usb_init();  // Silent - only notifies if controller found
}

// USB device handle for Xbox controller
static libusb_device_handle* g_xbox_handle = NULL;

static int try_usb_init(void) {
    // Load USB module first
    char module[256];
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libSceUsbd.sprx");
    int h = 0;
    sys_dynlib_load_prx(module, &h);

    // Init USB
    int ret = sceUsbdInit();
    if (ret != 0) return ret;

    // Scan for Xbox 360 controller
    libusb_device** dev_list = NULL;
    int32_t dev_count = sceUsbdGetDeviceList(&dev_list);
    if (dev_count <= 0 || dev_list == NULL) {
        return -1;
    }

    // Look for Xbox 360 controller (VID 0x045E, PID 0x028E)
    for (int i = 0; i < dev_count; i++) {
        struct libusb_device_descriptor desc;
        if (sceUsbdGetDeviceDescriptor(dev_list[i], &desc) == 0) {
            if (desc.idVendor == 0x045E && desc.idProduct == 0x028E) {
                ret = sceUsbdOpen(dev_list[i], &g_xbox_handle);
                if (ret == 0 && g_xbox_handle != NULL) {
                    ret = sceUsbdClaimInterface(g_xbox_handle, 0);
                    if (ret == 0) {
                        hook_notify("Xbox 360 connected!");
                        sceUsbdFreeDeviceList(dev_list);
                        return 0;
                    }
                    sceUsbdClose(g_xbox_handle);
                    g_xbox_handle = NULL;
                }
            }
        }
    }

    sceUsbdFreeDeviceList(dev_list);
    return -1;
}

// Cached Xbox report - updated by polling, read by hooks
static Xbox360Report g_cached_xbox_report;
static volatile int g_has_xbox_data = 0;
static uint64_t g_last_read_time = 0;

// Helper to inject Xbox data into pad data
static void inject_xbox_input(OrbisPadData* pData) {
    // Try USB init if not done
    if (!g_uhid_tried) {
        try_open_uhid();
    }

    // Read from Xbox controller if connected, but not too often
    if (g_xbox_handle != NULL) {
        uint64_t now = sceKernelGetProcessTime();

        // Read every 1ms (1000Hz) - edge of stability
        if (now - g_last_read_time > 1000) {
            g_last_read_time = now;

            Xbox360Report xbox_report;
            int32_t transferred = 0;

            // Read from interrupt endpoint (0x81 = EP1 IN)
            int ret = sceUsbdInterruptTransfer(
                g_xbox_handle,
                0x81,
                (unsigned char*)&xbox_report,
                sizeof(xbox_report),
                &transferred,
                2  // 2ms timeout for reliability
            );

            if (ret == 0 && transferred >= 20) {  // Xbox report is 20 bytes
                if (!g_xbox_active) {
                    g_xbox_active = 1;
                    hook_notify("Xbox input active!");
                }
                g_cached_xbox_report = xbox_report;
                g_has_xbox_data = 1;
            }
        }

        // Use cached data for translation
        if (g_has_xbox_data) {
            xbox360_to_ds4(&g_cached_xbox_report, pData);
        }
    }
}

// Hook scePadRead - replace DS4 data with Xbox data if available
int32_t scePadRead_hook(int32_t handle, OrbisPadData* pData, int32_t num) {
    // Call original function via Ext version
    int32_t ret = scePadReadExt(handle, pData, num);

    if (ret <= 0) {
        return ret;
    }

    // Inject Xbox data into each returned sample
    for (int i = 0; i < ret; i++) {
        inject_xbox_input(&pData[i]);
    }

    return ret;
}

// Hook scePadReadState - replace DS4 data with Xbox data if available
int32_t scePadReadState_hook(int32_t handle, OrbisPadData* pData) {
    // Call original function via Ext version
    int32_t ret = scePadReadStateExt(handle, pData);

    if (ret != 0) {
        return ret;
    }

    inject_xbox_input(pData);
    return ret;
}

int hooks_install(void) {
    if (g_hooks_installed) {
        return 0;
    }

    // Load libScePad like gamepad_helper does
    char module[256];
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libScePad.sprx");
    int h = 0;
    sys_dynlib_load_prx(module, &h);

    // Set up patcher for scePadReadExt (xor ecx, ecx)
    g_padReadExtPatcher = (Patcher*)malloc(sizeof(Patcher));
    if (g_padReadExtPatcher) {
        Patcher_Construct(g_padReadExtPatcher);
        uint8_t xor_ecx_ecx[5] = {0x31, 0xC9, 0x90, 0x90, 0x90};
        Patcher_Install_Patch(g_padReadExtPatcher, (uint64_t)scePadReadExt, xor_ecx_ecx, sizeof(xor_ecx_ecx));
    }

    // Set up patcher for scePadReadStateExt (xor edx, edx)
    g_padReadStateExtPatcher = (Patcher*)malloc(sizeof(Patcher));
    if (g_padReadStateExtPatcher) {
        Patcher_Construct(g_padReadStateExtPatcher);
        uint8_t xor_edx_edx[5] = {0x31, 0xD2, 0x90, 0x90, 0x90};
        Patcher_Install_Patch(g_padReadStateExtPatcher, (uint64_t)scePadReadStateExt, xor_edx_edx, sizeof(xor_edx_edx));
    }

    HOOK32(scePadRead);
    HOOK32(scePadReadState);

    g_hooks_installed = 1;
    return 0;
}

void hooks_remove(void) {
    if (!g_hooks_installed) return;

    // Close HID device if open
    if (g_uhid_fd >= 0) {
        sceKernelClose(g_uhid_fd);
        g_uhid_fd = -1;
    }

    UNHOOK(scePadRead);
    UNHOOK(scePadReadState);

    if (g_padReadExtPatcher) {
        Patcher_Destroy(g_padReadExtPatcher);
        free(g_padReadExtPatcher);
        g_padReadExtPatcher = NULL;
    }

    if (g_padReadStateExtPatcher) {
        Patcher_Destroy(g_padReadStateExtPatcher);
        free(g_padReadStateExtPatcher);
        g_padReadStateExtPatcher = NULL;
    }

    g_hooks_installed = 0;
}

int hooks_is_virtual_handle(int handle) { (void)handle; return 0; }
int hooks_handle_to_index(int handle) { (void)handle; return -1; }
