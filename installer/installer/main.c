/*
 * Xbox Controller Plugin Installer
 * Copies xbox_controller.prx to /data/GoldHEN/plugins/
 * and updates plugins.ini
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <orbis/libkernel.h>

// Notification helper
static void notify(const char* message) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Copy file from src to dst
static int copy_file(const char* src, const char* dst) {
    int src_fd = sceKernelOpen(src, 0x0000, 0);  // O_RDONLY
    if (src_fd < 0) {
        return -1;
    }

    // Get file size
    OrbisKernelStat stat;
    if (sceKernelFstat(src_fd, &stat) < 0) {
        sceKernelClose(src_fd);
        return -2;
    }

    // Read file into memory
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

    // Write to destination (create/truncate)
    int dst_fd = sceKernelOpen(dst, 0x0601, 0777);  // O_WRONLY | O_CREAT | O_TRUNC
    if (dst_fd < 0) {
        free(buffer);
        return -5;
    }

    ssize_t bytes_written = sceKernelWrite(dst_fd, buffer, stat.st_size);
    sceKernelClose(dst_fd);
    free(buffer);

    if (bytes_written != stat.st_size) {
        return -6;
    }

    return 0;
}

// Check if line exists in file
static int line_in_file(const char* filepath, const char* line) {
    int fd = sceKernelOpen(filepath, 0x0000, 0);  // O_RDONLY
    if (fd < 0) {
        return 0;  // File doesn't exist = line not in file
    }

    OrbisKernelStat stat;
    if (sceKernelFstat(fd, &stat) < 0 || stat.st_size == 0) {
        sceKernelClose(fd);
        return 0;
    }

    char* buffer = malloc(stat.st_size + 1);
    if (!buffer) {
        sceKernelClose(fd);
        return 0;
    }

    sceKernelRead(fd, buffer, stat.st_size);
    buffer[stat.st_size] = '\0';
    sceKernelClose(fd);

    int found = (strstr(buffer, line) != NULL);
    free(buffer);
    return found;
}

// Append line to file
static int append_to_file(const char* filepath, const char* content) {
    // O_WRONLY | O_CREAT | O_APPEND
    int fd = sceKernelOpen(filepath, 0x0409, 0777);
    if (fd < 0) {
        return -1;
    }

    ssize_t written = sceKernelWrite(fd, content, strlen(content));
    sceKernelClose(fd);

    return (written > 0) ? 0 : -1;
}

int main(void) {
    // Give system time to initialize
    sceKernelUsleep(500000);

    notify("Xbox Controller Installer");
    sceKernelUsleep(1000000);

    // Create plugins directory if needed
    sceKernelMkdir("/data/GoldHEN", 0777);
    sceKernelMkdir("/data/GoldHEN/plugins", 0777);

    // Source: bundled in app's assets folder
    // The app directory is /mnt/sandbox/PFSX00001_000/app0/
    const char* src_prx = "/app0/assets/xbox_controller.prx";
    const char* dst_prx = "/data/GoldHEN/plugins/xbox_controller.prx";
    const char* ini_path = "/data/GoldHEN/plugins.ini";
    const char* plugin_entry = "/data/GoldHEN/plugins/xbox_controller.prx";

    // Copy the .prx file
    int ret = copy_file(src_prx, dst_prx);
    if (ret < 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Copy failed! Error: %d", ret);
        notify(msg);
        sceKernelUsleep(3000000);

        // Exit app
        sceKernelSleep(2);
        return 1;
    }

    notify("Plugin copied!");
    sceKernelUsleep(1000000);

    // Update plugins.ini if needed
    if (!line_in_file(ini_path, plugin_entry)) {
        // Check if file exists and has [default] section
        if (!line_in_file(ini_path, "[default]")) {
            append_to_file(ini_path, "[default]\n");
        }
        append_to_file(ini_path, plugin_entry);
        append_to_file(ini_path, "\n");
        notify("plugins.ini updated!");
    } else {
        notify("plugins.ini already configured");
    }

    sceKernelUsleep(1500000);
    notify("Installation complete! Reboot PS4.");

    // Keep app alive briefly then exit
    sceKernelUsleep(3000000);

    return 0;
}
