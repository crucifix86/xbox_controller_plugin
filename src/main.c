/*
 * Xbox Controller Plugin for PS4 (GoldHEN)
 * TEST: Hooks only, no USB
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "hooks.h"

// OpenOrbis headers
#include <orbis/libkernel.h>

// GoldHEN SDK
#include <GoldHEN.h>

#define attr_public __attribute__((visibility("default")))

attr_public const char *g_pluginName = "xbox_controller";
attr_public const char *g_pluginDesc = "Xbox 360 Controller Support";
attr_public const char *g_pluginAuth = "xbox_controller_plugin";
attr_public uint32_t g_pluginVersion = 0x00000100;

static void notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

int32_t attr_public plugin_load(int32_t argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    hooks_install();
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    hooks_remove();
    notify("Xbox: Unloaded");
    return 0;
}

int module_start(size_t argc, const void* argv) {
    (void)argc;
    (void)argv;
    return 0;
}

int module_stop(size_t argc, const void* argv) {
    (void)argc;
    (void)argv;
    return 0;
}
