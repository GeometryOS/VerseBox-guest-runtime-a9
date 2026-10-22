#include "GuestPaths.h"
#include <alloca.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static bool environmentHas(char* const env[], const char* key) {
    size_t length = strlen(key);
    for (size_t i = 0; env && env[i]; ++i) {
        if (!strncmp(env[i], key, length) && env[i][length] == '=') return true;
    }
    return false;
}

static int guestExec(const char* path, char* const argv[], char* const env[], unsigned depth) {
    if (depth > 4) { errno = ELOOP; return -1; }
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    if (!mapped) return -1;
    if (mapped == path || !guestRoot()) return syscall(__NR_execve, path, argv, env);
    int fd = syscall(__NR_openat, AT_FDCWD, mapped, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    char prefix[257]{};
    ssize_t size = TEMP_FAILURE_RETRY(read(fd, prefix, sizeof(prefix) - 1));
    int saved = errno;
    close(fd);
    if (size < 0) { errno = saved; return -1; }
    if (syscall(__NR_faccessat, AT_FDCWD, mapped, X_OK) < 0) return -1;
    size_t argc = 0, envc = 0;
    while (argv && argv[argc]) if (++argc > 131072) { errno = E2BIG; return -1; }
    while (env && env[envc]) if (++envc > 131072) { errno = E2BIG; return -1; }
    if (argc + envc > 131072) { errno = E2BIG; return -1; }
    if (size >= 2 && prefix[0] == '#' && prefix[1] == '!') {
        char* end = strchr(prefix, '\n');
        if (!end) {
            if (size == sizeof(prefix) - 1) { errno = ENOEXEC; return -1; }
            end = prefix + size;
        }
        *end = '\0';
        while (end > prefix + 2 && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        char* interpreter = prefix + 2;
        while (*interpreter == ' ' || *interpreter == '\t') ++interpreter;
        if (!*interpreter) { errno = ENOEXEC; return -1; }
        char* option = interpreter;
        while (*option && *option != ' ' && *option != '\t') ++option;
        if (*option) {
            *option++ = '\0';
            while (*option == ' ' || *option == '\t') ++option;
        }
        char** arguments = static_cast<char**>(alloca((argc + 4) * sizeof(char*)));
        size_t count = 0;
        arguments[count++] = interpreter;
        if (*option) arguments[count++] = option;
        arguments[count++] = const_cast<char*>(path);
        for (size_t i = 1; i < argc; ++i) arguments[count++] = argv[i];
        arguments[count] = nullptr;
        return guestExec(interpreter, arguments, env, depth + 1);
    }
    Elf64_Ehdr header;
    if (size < sizeof(header)) { errno = ENOEXEC; return -1; }
    memcpy(&header, prefix, sizeof(header));
    if (memcmp(header.e_ident, ELFMAG, SELFMAG) ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_machine != EM_AARCH64 || header.e_type != ET_DYN) {
        errno = ENOEXEC;
        return -1;
    }
    const char* loader = getenv("A10VM_EXEC_LINKER");
    if (!loader || loader[0] != '/') { errno = EINVAL; return -1; }
    char** arguments = static_cast<char**>(alloca((argc + 3) * sizeof(char*)));
    arguments[0] = const_cast<char*>(loader);
    arguments[1] = const_cast<char*>(mapped);
    size_t count = 2;
    for (size_t i = 1; i < argc; ++i) arguments[count++] = argv[i];
    arguments[count] = nullptr;
    static const char* keys[] = {"A10VM_ROOTFS", "A10VM_HOST_ROOT", "A10VM_STATE_ROOT",
        "A10VM_EXEC_LINKER", "A10VM_PROPERTIES", "A10VM_LD_CONFIG"};
    char** environment = static_cast<char**>(alloca((envc + 8) * sizeof(char*)));
    count = 0;
    for (size_t i = 0; i < envc; ++i) {
        if (strncmp(env[i], "A10VM_EXEC_ARGV0=", 17)) environment[count++] = env[i];
    }
    char settings[8 * PATH_MAX];
    size_t used = 0;
    for (const char* key : keys) {
        const char* value = getenv(key);
        if (!value || environmentHas(env, key)) continue;
        size_t length = strlen(key) + strlen(value) + 2;
        if (length > sizeof(settings) - used) { errno = E2BIG; return -1; }
        char* entry = settings + used;
        strcpy(entry, key);
        strcat(entry, "=");
        strcat(entry, value);
        environment[count++] = entry;
        used += length;
    }
    const char* argv0 = argc ? argv[0] : path;
    size_t length = strlen(argv0) + sizeof("A10VM_EXEC_ARGV0=");
    if (length > sizeof(settings) - used) { errno = E2BIG; return -1; }
    char* entry = settings + used;
    strcpy(entry, "A10VM_EXEC_ARGV0=");
    strcat(entry, argv0);
    environment[count++] = entry;
    environment[count] = nullptr;
    return syscall(__NR_execve, loader, arguments, environment);
}

int execve(const char* path, char* const argv[], char* const env[]) {
    return guestExec(path, argv, env, 0);
}
