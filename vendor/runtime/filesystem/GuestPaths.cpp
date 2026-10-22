#include "GuestPaths.h"
#include <async_safe/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static bool beneath(const char* path, const char* directory) {
    if (!directory || directory[0] != '/') return false;
    size_t size = strlen(directory);
    return size > 1 && !strncmp(path, directory, size) &&
           (path[size] == '/' || path[size] == '\0');
}

const char* guestRoot() {
    const char* root = getenv("A10VM_ROOTFS");
    return root && root[0] ? root : nullptr;
}

bool guestPhysicalPath(const char* path) { return path && beneath(path, guestRoot()); }

static bool hostPath(const char* path) {
    return beneath(path, getenv("A10VM_HOST_ROOT")) || beneath(path, getenv("A10VM_STATE_ROOT"));
}

static bool guestAbsolute(const char* path) {
    static const char* directories[] = {"/system", "/vendor", "/product", "/product_services", "/odm", "/oem",
        "/bin", "/sbin", "/sdcard", "/bugreports", "/config",
        "/apex", "/data", "/cache", "/storage", "/mnt", "/metadata", "/etc", "/dev/socket", "/dev/input",
        // Scheduler groups: API 28's set_sched_policy() opens
        // /dev/cpuctl/<group>/tasks, and Android 10's cgroup layer uses cpuset.
        // Without these a guest cannot create the nodes an init would have.
        "/dev/cpuctl", "/dev/cpuset"};
    if (!strcmp(path, "/") || !strcmp(path, "/init") || !strcmp(path, "/dev") ||
            !strcmp(path, "/default.prop") || !strcmp(path, "/charger") || !strcmp(path, "/ueventd.rc") ||
            !strcmp(path, "/a10vm-init") || !strncmp(path, "/init.", 6)) return true;
    for (const char* directory : directories) if (beneath(path, directory)) return true;
    return false;
}

static bool fontResource(const char* path) {
    if (!strcmp(path, "/system/etc/fonts.xml") ||
            !strcmp(path, "/system/product/etc/fonts_customization.xml")) return true;
    constexpr const char* fonts = "/system/fonts/";
    size_t prefix = strlen(fonts);
    return !strncmp(path, fonts, prefix) && path[prefix] &&
           !strchr(path + prefix, '/') && strcmp(path + prefix, ".") && strcmp(path + prefix, "..");
}

static bool copy(char* output, const char* input) {
    if (strlcpy(output, input, PATH_MAX) < PATH_MAX) return true;
    errno = ENAMETOOLONG;
    return false;
}

static bool append(char* output, const char* input) {
    size_t size = strlen(output);
    if (strlcpy(output + size, input, PATH_MAX - size) < PATH_MAX - size) return true;
    errno = ENAMETOOLONG;
    return false;
}

static void parent(char* path) {
    char* slash = nullptr;
    for (char* cursor = path; *cursor; ++cursor) if (*cursor == '/') slash = cursor;
    if (slash == path) path[1] = '\0';
    else if (slash) *slash = '\0';
}

static bool directoryPath(int dirfd, char* output) {
    if (dirfd == AT_FDCWD) return syscall(__NR_getcwd, output, PATH_MAX) > 0;
    char descriptor[64];
    async_safe_format_buffer(descriptor, sizeof(descriptor), "/proc/self/fd/%d", dirfd);
    ssize_t size = syscall(__NR_readlinkat, AT_FDCWD, descriptor, output, PATH_MAX - 1);
    if (size < 0) return false;
    output[size] = '\0';
    return true;
}

const char* guestPath(int dirfd, const char* path, char (&buffer)[PATH_MAX], bool follow) {
    if (!path) { errno = EFAULT; return nullptr; }
    const char* root = guestRoot();
    if (!root || !path[0]) return path;
    size_t rootSize = strlen(root);
    if (root[0] != '/' || rootSize <= 1 || rootSize >= PATH_MAX || root[rootSize - 1] == '/') {
        errno = EINVAL;
        return nullptr;
    }
    if (beneath(path, root) || hostPath(path)) return path;
    char pending[PATH_MAX];
    if (path[0] == '/') {
        if (!guestAbsolute(path)) return path;
        if (!copy(pending, path)) return nullptr;
    } else {
        char base[PATH_MAX];
        int saved = errno;
        if (!directoryPath(dirfd, base)) return path;
        errno = saved;
        if (!beneath(base, root)) return path;
        if (!copy(pending, base + rootSize) || !append(pending, "/") || !append(pending, path)) {
            return nullptr;
        }
    }
    // These packaged resources are regular copies, not guest symlinks. Chromium's
    // renderer permits opening them but denies the readlinkat used by the general mapper.
    if (fontResource(pending)) {
        if (!copy(buffer, root) || !append(buffer, pending)) return nullptr;
        return buffer;
    }
    char resolved[PATH_MAX] = "/";
    unsigned links = 0;
    while (pending[0]) {
        char* begin = pending;
        while (*begin == '/') ++begin;
        if (!*begin) break;
        char* end = strchr(begin, '/');
        char remaining[PATH_MAX];
        if (!copy(remaining, end ? end : "")) return nullptr;
        if (end) *end = '\0';
        if (!strcmp(begin, "..")) parent(resolved);
        else if (strcmp(begin, ".")) {
            if (strcmp(resolved, "/") && !append(resolved, "/")) return nullptr;
            if (!append(resolved, begin) || !copy(buffer, root) || !append(buffer, resolved)) return nullptr;
            if (follow || remaining[0]) {
                char target[PATH_MAX];
                int saved = errno;
                ssize_t size = syscall(__NR_readlinkat, AT_FDCWD, buffer, target, PATH_MAX - 1);
                if (size >= 0) {
                    if (++links > 40) { errno = ELOOP; return nullptr; }
                    if (size == PATH_MAX - 1) { errno = ENAMETOOLONG; return nullptr; }
                    target[size] = '\0';
                    if (hostPath(target) && !beneath(target, root)) {
                        if (!copy(buffer, target) || !append(buffer, remaining)) return nullptr;
                        errno = saved;
                        return buffer;
                    }
                    const char* next = beneath(target, root) ? target + rootSize : target;
                    if (target[0] == '/') copy(resolved, "/");
                    else parent(resolved);
                    if (!copy(pending, next) || !append(pending, remaining)) return nullptr;
                    errno = saved;
                    continue;
                }
                if (errno != EINVAL && errno != ENOENT) return nullptr;
                if (remaining[0]) {
                    struct stat info;
                    if (syscall(__NR_newfstatat, AT_FDCWD, buffer, &info, 0) < 0) return nullptr;
                    if (!S_ISDIR(info.st_mode)) { errno = ENOTDIR; return nullptr; }
                }
                errno = saved;
            }
        }
        if (!copy(pending, remaining)) return nullptr;
    }
    if (!copy(buffer, root) || !append(buffer, resolved)) return nullptr;
    return buffer;
}

const char* guestMkdirPath(int dirfd, const char* path, char (&buffer)[PATH_MAX]) {
    if (!path || !path[0]) return guestPath(dirfd, path, buffer, false);
    size_t length = strlen(path);
    if (path[length - 1] != '/') return guestPath(dirfd, path, buffer, false);
    if (length >= PATH_MAX) { errno = ENAMETOOLONG; return nullptr; }
    char directory[PATH_MAX];
    memcpy(directory, path, length + 1);
    // mkdir accepts trailing slashes without requiring the new leaf to exist.
    while (length > 1 && directory[length - 1] == '/') directory[--length] = '\0';
    const char* mapped = guestPath(dirfd, directory, buffer, false);
    if (!mapped) return nullptr;
    if (mapped != buffer && !copy(buffer, mapped)) return nullptr;
    return buffer;
}
