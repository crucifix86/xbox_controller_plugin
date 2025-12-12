/*
 * Hybrid approach: Hook scePad like gamepad_helper does
 * Read Xbox controller via USB, inject into scePadReadState
 *
 * STABILITY: All initialization done upfront in plugin_load,
 * hooks only do lightweight data injection
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
#include <orbis/UserService.h>

#include <GoldHEN.h>
#include <Detour.h>
#include <Patcher.h>
#include <Utilities.h>

// External function declarations
extern int32_t scePadReadExt(int32_t handle, OrbisPadData* pData, int32_t num);
extern int32_t scePadReadStateExt(int32_t handle, OrbisPadData* pData);
extern int sys_dynlib_load_prx(const char* path, int* handle);
extern const char* sceKernelGetFsSandboxRandomWord(void);

// Virtual user/controller constants
// Use real PS4 user ID for Player 2 (from /user/home/ directory)
#define XBOX_VIRTUAL_USER_ID    0x13be5cb8  // Second PS4 user profile
#define XBOX_VIRTUAL_PAD_HANDLE 1001        // Our fake pad handle

// Function pointer types for HOOK_CONTINUE
typedef int32_t (*scePadOpen_t)(int32_t, int32_t, int32_t, void*);
typedef int32_t (*scePadClose_t)(int32_t);
typedef int32_t (*scePadGetControllerInformation_t)(int32_t, OrbisPadInformation*);
typedef int32_t (*sceUserServiceGetLoginUserIdList_t)(OrbisUserServiceLoginUserIdList*);

// Hooks for pad and user service functions
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);
HOOK_INIT(scePadOpen);
HOOK_INIT(scePadClose);
HOOK_INIT(scePadGetControllerInformation);
HOOK_INIT(sceUserServiceGetLoginUserIdList);

// Patchers
static Patcher* g_padReadExtPatcher = NULL;
static Patcher* g_padReadStateExtPatcher = NULL;

// State flags
static int g_hooks_installed = 0;
static int g_usb_initialized = 0;
static int g_pad_prx_loaded = 0;
static int g_usb_prx_loaded = 0;
static int g_user_prx_loaded = 0;

// Virtual controller state
static int g_virtual_pad_open = 0;      // Is our virtual pad currently open?
static int g_xbox_connected = 0;        // Is Xbox controller physically connected?

// USB device handle for Xbox controller
static libusb_device_handle* g_xbox_handle = NULL;

static void hook_notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Initialize USB subsystem - call ONCE from plugin_load, NOT from hooks
int hooks_init_usb(void) {
    if (g_usb_initialized) return 0;

    // Load USB module
    char module[256];
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libSceUsbd.sprx");
    int h = 0;
    int ret = sys_dynlib_load_prx(module, &h);
    if (ret < 0 || h == 0) {
        // USB PRX failed to load - not fatal, just no Xbox support
        return -1;
    }
    g_usb_prx_loaded = 1;

    // Init USB subsystem
    ret = sceUsbdInit();
    if (ret != 0) {
        return -1;
    }
    g_usb_initialized = 1;

    // Scan for Xbox 360 controller
    libusb_device** dev_list = NULL;
    int32_t dev_count = sceUsbdGetDeviceList(&dev_list);
    if (dev_count <= 0 || dev_list == NULL) {
        // No USB devices - that's okay
        return 0;
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
                        g_xbox_connected = 1;
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
    return 0;  // USB init succeeded even if no Xbox found
}

// Cached Xbox report - updated by polling, read by hooks
static Xbox360Report g_cached_xbox_report;
static volatile int g_has_xbox_data = 0;
static uint64_t g_last_read_time = 0;
static int g_xbox_active = 0;

// Helper to inject Xbox data into pad data
// STABILITY: No initialization here - just read if connected
static void inject_xbox_input(OrbisPadData* pData) {
    // Only try to read if Xbox handle is valid (set during plugin_load)
    if (g_xbox_handle == NULL) {
        return;  // No Xbox connected, pass through DS4 data unchanged
    }

    uint64_t now = sceKernelGetProcessTime();

    // Read every 1ms (1000Hz)
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
            2  // 2ms timeout
        );

        if (ret == 0 && transferred >= 20) {
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

// ============================================
// User Service Hook - Inject virtual user
// ============================================

int32_t sceUserServiceGetLoginUserIdList_hook(OrbisUserServiceLoginUserIdList* userIdList) {
    // Call original first via HOOK_CONTINUE
    int32_t ret = HOOK_CONTINUE(sceUserServiceGetLoginUserIdList, sceUserServiceGetLoginUserIdList_t, userIdList);
    if (ret != 0 || userIdList == NULL) {
        return ret;
    }

    // Only inject if Xbox controller is connected
    if (!g_xbox_connected) {
        return ret;
    }

    // Find an empty slot and inject our virtual user
    for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
        if (userIdList->userId[i] == ORBIS_USER_SERVICE_USER_ID_INVALID) {
            userIdList->userId[i] = XBOX_VIRTUAL_USER_ID;
            hook_notify("Injected Player 2 user");
            break;
        }
    }

    return ret;
}

// ============================================
// Pad Open/Close Hooks - Handle virtual controller
// ============================================

int32_t scePadOpen_hook(int32_t userId, int32_t type, int32_t index, void* param) {
    // Check if this is for our virtual user
    if (userId == XBOX_VIRTUAL_USER_ID && g_xbox_connected) {
        g_virtual_pad_open = 1;
        hook_notify("Xbox Player 2 ready!");
        return XBOX_VIRTUAL_PAD_HANDLE;
    }

    // Pass through to real scePadOpen via HOOK_CONTINUE
    return HOOK_CONTINUE(scePadOpen, scePadOpen_t, userId, type, index, param);
}

int32_t scePadClose_hook(int32_t handle) {
    // Check if closing our virtual pad
    if (handle == XBOX_VIRTUAL_PAD_HANDLE) {
        g_virtual_pad_open = 0;
        return 0;
    }

    // Pass through to real scePadClose via HOOK_CONTINUE
    return HOOK_CONTINUE(scePadClose, scePadClose_t, handle);
}

// ============================================
// Controller Info Hook - Report virtual controller
// ============================================

int32_t scePadGetControllerInformation_hook(int32_t handle, OrbisPadInformation* info) {
    // Check if querying our virtual pad
    if (handle == XBOX_VIRTUAL_PAD_HANDLE) {
        if (info != NULL) {
            memset(info, 0, sizeof(OrbisPadInformation));
            info->connected = g_xbox_connected ? 1 : 0;
            info->connectionType = ORBIS_PAD_CONNECTION_TYPE_STANDARD;
            info->deviceClass = ORBIS_PAD_DEVICE_CLASS_PAD;
            // Fake touchpad info (Xbox has none)
            info->touchpadDensity = 1.0f;
            info->touchResolutionX = 1920;
            info->touchResolutionY = 943;
        }
        return 0;
    }

    // Pass through to real function via HOOK_CONTINUE
    return HOOK_CONTINUE(scePadGetControllerInformation, scePadGetControllerInformation_t, handle, info);
}

// ============================================
// Pad Read Hooks - Only inject on virtual handle
// ============================================

int32_t scePadRead_hook(int32_t handle, OrbisPadData* pData, int32_t num) {
    // Check if this is our virtual pad
    if (handle == XBOX_VIRTUAL_PAD_HANDLE) {
        if (!g_virtual_pad_open || pData == NULL || num <= 0) {
            return 0;
        }

        // Fill with Xbox data
        for (int i = 0; i < num; i++) {
            memset(&pData[i], 0, sizeof(OrbisPadData));
            pData[i].connected = g_xbox_connected ? 1 : 0;
            pData[i].timestamp = sceKernelGetProcessTime();
            // Neutral stick positions
            pData[i].leftStick.x = 128;
            pData[i].leftStick.y = 128;
            pData[i].rightStick.x = 128;
            pData[i].rightStick.y = 128;

            if (g_xbox_connected) {
                inject_xbox_input(&pData[i]);
            }
        }
        return num;
    }

    // For real DS4 handle - pass through unchanged
    int32_t ret = scePadReadExt(handle, pData, num);
    return ret;
}

int32_t scePadReadState_hook(int32_t handle, OrbisPadData* pData) {
    // Check if this is our virtual pad
    if (handle == XBOX_VIRTUAL_PAD_HANDLE) {
        if (!g_virtual_pad_open || pData == NULL) {
            return -1;
        }

        // Fill with Xbox data
        memset(pData, 0, sizeof(OrbisPadData));
        pData->connected = g_xbox_connected ? 1 : 0;
        pData->timestamp = sceKernelGetProcessTime();
        // Neutral stick positions
        pData->leftStick.x = 128;
        pData->leftStick.y = 128;
        pData->rightStick.x = 128;
        pData->rightStick.y = 128;

        if (g_xbox_connected) {
            inject_xbox_input(pData);
        }
        return 0;
    }

    // For real DS4 handle - pass through unchanged
    int32_t ret = scePadReadStateExt(handle, pData);
    return ret;
}

int hooks_install(void) {
    if (g_hooks_installed) {
        return 0;
    }

    char module[256];
    int h = 0;
    int ret;

    // Load libScePad - MUST succeed for hooks to work
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libScePad.sprx");
    ret = sys_dynlib_load_prx(module, &h);
    if (ret < 0 || h == 0) {
        hook_notify("Xbox: Pad lib failed");
        return -1;
    }
    g_pad_prx_loaded = 1;

    // Load libSceUserService for virtual user injection
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libSceUserService.sprx");
    h = 0;
    ret = sys_dynlib_load_prx(module, &h);
    if (ret >= 0 && h != 0) {
        g_user_prx_loaded = 1;
    }
    // User service is optional - continue even if it fails

    // Verify scePadReadExt exists before patching
    if ((uint64_t)scePadReadExt == 0) {
        hook_notify("Xbox: No PadReadExt");
        return -1;
    }

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

    // Install pad hooks
    HOOK32(scePadRead);
    HOOK32(scePadReadState);
    HOOK32(scePadOpen);
    HOOK32(scePadClose);
    HOOK32(scePadGetControllerInformation);

    // Install user service hook if library loaded
    if (g_user_prx_loaded) {
        HOOK32(sceUserServiceGetLoginUserIdList);
    }

    g_hooks_installed = 1;
    return 0;
}

void hooks_remove(void) {
    // Unhook first to stop any hook calls
    if (g_hooks_installed) {
        UNHOOK(scePadRead);
        UNHOOK(scePadReadState);
        UNHOOK(scePadOpen);
        UNHOOK(scePadClose);
        UNHOOK(scePadGetControllerInformation);

        if (g_user_prx_loaded) {
            UNHOOK(sceUserServiceGetLoginUserIdList);
        }

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

    // Clean up USB resources
    if (g_xbox_handle != NULL) {
        sceUsbdReleaseInterface(g_xbox_handle, 0);
        sceUsbdClose(g_xbox_handle);
        g_xbox_handle = NULL;
    }

    if (g_usb_initialized) {
        sceUsbdExit();
        g_usb_initialized = 0;
    }

    // Reset state
    g_has_xbox_data = 0;
    g_xbox_active = 0;
    g_xbox_connected = 0;
    g_virtual_pad_open = 0;
}

int hooks_is_virtual_handle(int handle) {
    return (handle == XBOX_VIRTUAL_PAD_HANDLE) ? 1 : 0;
}

int hooks_handle_to_index(int handle) {
    return (handle == XBOX_VIRTUAL_PAD_HANDLE) ? 0 : -1;
}
