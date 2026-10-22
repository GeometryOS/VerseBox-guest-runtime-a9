#include "GuestProcMaps.h"
#include "GuestPaths.h"
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <linux/memfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

struct LibraryAlias {
    LibraryAlias* next;
    dev_t device;
    ino_t inode;
    char path[1];
};

static int collectAlias(dl_phdr_info* info, size_t, void* data) {
    if (!info->dlpi_name || info->dlpi_name[0] != '/' || guestPhysicalPath(info->dlpi_name)) return 0;
    char buffer[PATH_MAX];
    const char* physical = guestPath(AT_FDCWD, info->dlpi_name, buffer);
    struct stat metadata;
    if (!physical || !strcmp(physical, info->dlpi_name) ||
            syscall(__NR_newfstatat, AT_FDCWD, physical, &metadata, 0) != 0) return 0;
    auto* alias = static_cast<LibraryAlias*>(malloc(sizeof(LibraryAlias) + strlen(info->dlpi_name)));
    if (!alias) return 1;
    auto** aliases = static_cast<LibraryAlias**>(data);
    alias->next = *aliases;
    alias->device = metadata.st_dev;
    alias->inode = metadata.st_ino;
    strcpy(alias->path, info->dlpi_name);
    *aliases = alias;
    return 0;
}

static bool procMapsPath(const char* path) {
    if (strncmp(path, "/proc/", 6)) return false;
    const char* cursor = path + 6;
    char* end;
    strtoul(cursor, &end, 10);
    if (end == cursor) return false;
    cursor = end;
    if (!strncmp(cursor, "/task/", 6)) {
        cursor += 6;
        strtoul(cursor, &end, 10);
        if (end == cursor) return false;
        cursor = end;
    }
    return !strcmp(cursor, "/maps");
}

static bool writeAll(int descriptor, const char* text, size_t size) {
    while (size) {
        ssize_t written = syscall(__NR_write, descriptor, text, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        text += written;
        size -= written;
    }
    return true;
}

static bool writeRow(int descriptor, const char* line, const LibraryAlias* aliases) {
    unsigned deviceMajor, deviceMinor;
    unsigned long long inode;
    int start = 0;
    if (sscanf(line, "%*s %*s %*s %x:%x %llu %n", &deviceMajor, &deviceMinor, &inode, &start) != 3 ||
            !start || line[start] != '/') return writeAll(descriptor, line, strlen(line));
    const char* path = line + start;
    const char* logical = nullptr;
    for (auto* alias = aliases; alias; alias = alias->next) {
        if (alias->inode == inode && major(alias->device) == deviceMajor && minor(alias->device) == deviceMinor) {
            logical = alias->path;
            break;
        }
    }
    if (logical) {
        size_t size = strlen(path);
        const char* suffix = " (deleted)\n";
        size_t suffixSize = strlen(suffix);
        bool deleted = size >= suffixSize && !strcmp(path + size - suffixSize, suffix);
        return writeAll(descriptor, line, start) && writeAll(descriptor, logical, strlen(logical)) &&
                writeAll(descriptor, deleted ? suffix : "\n", deleted ? suffixSize : 1);
    }
    if (guestPhysicalPath(path)) {
        logical = path + strlen(guestRoot());
        return writeAll(descriptor, line, start) && writeAll(descriptor, logical, strlen(logical));
    }
    return writeAll(descriptor, line, strlen(line));
}

int guestProcMaps(int descriptor, const char* path, int flags) {
    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (!guestRoot() || strcmp(basename, "maps") || (flags & O_ACCMODE) != O_RDONLY ||
            (flags & (O_PATH | O_CREAT | O_TRUNC))) return descriptor;
    int saved = errno;
    char link[64], actual[PATH_MAX];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", descriptor);
    ssize_t length = syscall(__NR_readlinkat, AT_FDCWD, link, actual, sizeof(actual) - 1);
    if (length < 0) { errno = saved; return descriptor; }
    actual[length] = '\0';
    if (!procMapsPath(actual)) { errno = saved; return descriptor; }
    int snapshot = syscall(__NR_memfd_create, "a10vm-proc-maps", MFD_CLOEXEC);
    if (snapshot < 0) { errno = saved; return descriptor; }
    int copy = dup(descriptor);
    FILE* source = copy >= 0 ? fdopen(copy, "r") : nullptr;
    if (!source) {
        if (copy >= 0) close(copy);
        close(snapshot);
        errno = saved;
        return descriptor;
    }
    LibraryAlias* aliases = nullptr;
    bool complete = dl_iterate_phdr(collectAlias, &aliases) == 0;
    char input[4096], line[PATH_MAX + 512];
    setvbuf(source, input, _IOFBF, sizeof(input));
    while (complete && fgets(line, sizeof(line), source)) complete = writeRow(snapshot, line, aliases);
    complete &= !ferror(source);
    fclose(source);
    while (aliases) {
        auto* next = aliases->next;
        free(aliases);
        aliases = next;
    }
    int result = -1;
    if (complete) {
        snprintf(link, sizeof(link), "/proc/self/fd/%d", snapshot);
        // A memfd is writable initially; reopen it to retain the caller's read-only descriptor flags.
        result = syscall(__NR_openat, AT_FDCWD, link, flags, 0);
    }
    close(snapshot);
    if (result >= 0) close(descriptor);
    else {
        syscall(__NR_lseek, descriptor, 0, SEEK_SET);
        result = descriptor;
    }
    errno = saved;
    return result;
}
