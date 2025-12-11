/*
 * Xbox Controller Plugin Manager
 * Toggle: Install or Uninstall the plugin
 * - If not installed: copies prx and updates plugins.ini
 * - If installed: removes from plugins.ini (disables)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <orbis/libkernel.h>

#define PLUGIN_PATH "/data/GoldHEN/plugins/xbox_controller.prx"
#define INI_PATH "/data/GoldHEN/plugins.ini"

// Notification helper
static void notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Check if file exists
static int file_exists(const char* path) {
    int fd = sceKernelOpen(path, 0x0000, 0);  // O_RDONLY
    if (fd >= 0) {
        sceKernelClose(fd);
        return 1;
    }
    return 0;
}

// Copy file from src to dst
static int copy_file(const char* src, const char* dst) {
    int src_fd = sceKernelOpen(src, 0x0000, 0);  // O_RDONLY
    if (src_fd < 0) {
        return -1;
    }

    OrbisKernelStat stat;
    if (sceKernelFstat(src_fd, &stat) < 0) {
        sceKernelClose(src_fd);
        return -2;
    }

    void* buffer = malloc(stat.st_size);
    if (!buffer) {
        sceKernelClose(src_fd);
        return -3;
    }

    ssize_t bytes_read = sceKernelRead(src_fd, buffer, stat.st_size);
    sceKernelClose(src_fd);

    if (bytes_read != stat.st_size) {
        free(buffer);
        return -4;
    }

    int dst_fd = sceKernelOpen(dst, 0x0601, 0777);  // O_WRONLY | O_CREAT | O_TRUNC
    if (dst_fd < 0) {
        free(buffer);
        return -5;
    }

    ssize_t bytes_written = sceKernelWrite(dst_fd, buffer, stat.st_size);
    sceKernelClose(dst_fd);
    free(buffer);

    return (bytes_written == stat.st_size) ? 0 : -6;
}

// Read entire file into malloc'd buffer (caller frees)
static char* read_file(const char* path, size_t* out_size) {
    int fd = sceKernelOpen(path, 0x0000, 0);
    if (fd < 0) return NULL;

    OrbisKernelStat stat;
    if (sceKernelFstat(fd, &stat) < 0 || stat.st_size == 0) {
        sceKernelClose(fd);
        return NULL;
    }

    char* buffer = malloc(stat.st_size + 1);
    if (!buffer) {
        sceKernelClose(fd);
        return NULL;
    }

    sceKernelRead(fd, buffer, stat.st_size);
    buffer[stat.st_size] = '\0';
    sceKernelClose(fd);

    if (out_size) *out_size = stat.st_size;
    return buffer;
}

// Write buffer to file
static int write_file(const char* path, const char* content, size_t size) {
    int fd = sceKernelOpen(path, 0x0601, 0777);  // O_WRONLY | O_CREAT | O_TRUNC
    if (fd < 0) return -1;

    ssize_t written = sceKernelWrite(fd, content, size);
    sceKernelClose(fd);
    return (written == (ssize_t)size) ? 0 : -1;
}

// Check if plugin is enabled in plugins.ini
static int plugin_is_enabled(void) {
    char* ini = read_file(INI_PATH, NULL);
    if (!ini) return 0;

    // Check for uncommented plugin path
    char* line = ini;
    int enabled = 0;

    while (*line) {
        // Skip whitespace
        while (*line == ' ' || *line == '\t') line++;

        // Check if this line has our plugin (not commented)
        if (*line != ';' && *line != '#') {
            if (strstr(line, "xbox_controller.prx") != NULL) {
                enabled = 1;
                break;
            }
        }

        // Move to next line
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }

    free(ini);
    return enabled;
}

// Remove or comment out plugin from plugins.ini
static int disable_plugin(void) {
    size_t size;
    char* ini = read_file(INI_PATH, &size);
    if (!ini) return -1;

    // Create new buffer for modified ini
    char* new_ini = malloc(size + 16);  // Extra space for safety
    if (!new_ini) {
        free(ini);
        return -1;
    }

    char* src = ini;
    char* dst = new_ini;

    while (*src) {
        char* line_start = src;

        // Find end of line
        while (*src && *src != '\n') src++;
        int line_len = src - line_start;
        if (*src == '\n') src++;

        // Check if this line contains our plugin
        char line_copy[512];
        if (line_len < 511) {
            memcpy(line_copy, line_start, line_len);
            line_copy[line_len] = '\0';

            if (strstr(line_copy, "xbox_controller.prx") != NULL) {
                // Skip this line entirely (don't copy it)
                continue;
            }
        }

        // Copy line to output
        memcpy(dst, line_start, line_len);
        dst += line_len;
        *dst++ = '\n';
    }
    *dst = '\0';

    int ret = write_file(INI_PATH, new_ini, dst - new_ini);

    free(ini);
    free(new_ini);
    return ret;
}

// Add plugin to plugins.ini
static int enable_plugin(void) {
    size_t size = 0;
    char* ini = read_file(INI_PATH, &size);

    // Check if [default] section exists
    int has_default = ini && strstr(ini, "[default]") != NULL;

    int fd = sceKernelOpen(INI_PATH, 0x0409, 0777);  // O_WRONLY | O_CREAT | O_APPEND
    if (fd < 0) {
        if (ini) free(ini);
        return -1;
    }

    if (!has_default) {
        sceKernelWrite(fd, "[default]\n", 10);
    }
    // Write path and newline separately (don't include null terminator)
    sceKernelWrite(fd, PLUGIN_PATH, strlen(PLUGIN_PATH));
    sceKernelWrite(fd, "\n", 1);
    sceKernelClose(fd);

    if (ini) free(ini);
    return 0;
}

int main(void) {
    sceKernelUsleep(500000);

    notify("Xbox Controller Manager");
    sceKernelUsleep(1000000);

    // Create directories if needed
    sceKernelMkdir("/data/GoldHEN", 0777);
    sceKernelMkdir("/data/GoldHEN/plugins", 0777);

    // Check current state
    int prx_exists = file_exists(PLUGIN_PATH);
    int is_enabled = plugin_is_enabled();

    if (is_enabled) {
        // Currently enabled -> Disable it
        notify("Disabling Xbox Controller...");
        sceKernelUsleep(1000000);

        if (disable_plugin() == 0) {
            notify("Plugin DISABLED!");
            sceKernelUsleep(1000000);
            notify("Reboot PS4 to apply.");
        } else {
            notify("Failed to disable!");
        }
    } else {
        // Not enabled -> Install/Enable it
        notify("Installing Xbox Controller...");
        sceKernelUsleep(1000000);

        // Copy PRX if not present
        if (!prx_exists) {
            notify("Copying plugin file...");
            sceKernelUsleep(500000);

            int ret = copy_file("/app0/assets/xbox_controller.prx", PLUGIN_PATH);
            if (ret < 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Copy failed! Error: %d", ret);
                notify(msg);
                sceKernelUsleep(5000000);
            } else {
                notify("Plugin file copied OK!");
                sceKernelUsleep(1000000);
            }
        } else {
            notify("Plugin file exists, skipping copy");
            sceKernelUsleep(1000000);
        }

        // Enable in plugins.ini
        notify("Updating plugins.ini...");
        sceKernelUsleep(500000);

        int ini_ret = enable_plugin();
        if (ini_ret == 0) {
            notify("plugins.ini updated OK!");
            sceKernelUsleep(1000000);
            notify("Plugin ENABLED! Reboot PS4.");
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "INI update failed: %d", ini_ret);
            notify(msg);
            sceKernelUsleep(3000000);
        }
    }

    sceKernelUsleep(3000000);

    // Exit gracefully - PS4 apps shouldn't just return
    for (;;) {
        sceKernelUsleep(1000000);
    }
    return 0;
}
