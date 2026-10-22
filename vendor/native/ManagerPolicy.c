#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

struct service_audit {
    pid_t pid;
    uid_t uid;
    const char* name;
};

static int private_endpoint(const char* key) {
    const char* path = getenv(key);
    struct stat status;
    uid_t owner = syscall(__NR_getuid);
    return owner >= 10000 && path && path[0] == '/' &&
           stat(path, &status) == 0 && S_ISSOCK(status.st_mode) &&
           status.st_uid == owner && (status.st_mode & 077) == 0;
}

// Only the guest manager loads this policy; Binder transport checks SO_PEERCRED.
uid_t multiuser_get_app_id(uid_t uid) {
    if (private_endpoint("A10VM_BINDER_PATH") && uid == (uid_t)syscall(__NR_getuid)) return 1000;
    return uid % 100000;
}

int selinux_check_access(const char* source, const char* target, const char* object_class,
                        const char* permission, void* audit_data) {
    const struct service_audit* audit = audit_data;
    if (source && target && object_class && permission &&
        !strcmp(object_class, "hwservice_manager") &&
        (!strcmp(permission, "add") || !strcmp(permission, "find") || !strcmp(permission, "list")) &&
        private_endpoint("A10VM_HWBINDER_PATH")) return 0;
    if (private_endpoint("A10VM_BINDER_PATH") && source && target && object_class && permission && audit &&
        !strcmp(object_class, "service_manager") &&
        (!strcmp(permission, "add") || !strcmp(permission, "find") || !strcmp(permission, "list")) &&
        audit->pid > 0) return 0;
    errno = EACCES;
    return -1;
}
