#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static gid_t* groups;
static size_t group_count;
static struct __user_cap_data_struct capabilities[2];

static int mapped(void) {
    return getenv("A10VM_ROOTFS") && getenv("A10VM_GUEST_UID");
}

static int assign(const char* key, unsigned int id) {
    if (id == (unsigned int)-1) { errno = EINVAL; return -1; }
    char value[16];
    snprintf(value, sizeof(value), "%u", id);
    return setenv(key, value, 1);
}

int setuid(uid_t uid) {
    return mapped() ? assign("A10VM_GUEST_UID", uid) : syscall(__NR_setuid, uid);
}

int setgid(gid_t gid) {
    return mapped() ? assign("A10VM_GUEST_GID", gid) : syscall(__NR_setgid, gid);
}

int setgroups(size_t count, const gid_t* values) {
    if (!mapped()) return syscall(__NR_setgroups, count, values);
    if (count > 65536) { errno = EINVAL; return -1; }
    if (count && !values) { errno = EFAULT; return -1; }
    gid_t* copy = count ? malloc(count * sizeof(gid_t)) : NULL;
    if (count && !copy) return -1;
    if (count) memcpy(copy, values, count * sizeof(gid_t));
    free(groups);
    groups = copy;
    group_count = count;
    return 0;
}

int getgroups(int count, gid_t* values) {
    if (!mapped()) return syscall(__NR_getgroups, count, values);
    if (count < 0 || (count && (size_t)count < group_count)) { errno = EINVAL; return -1; }
    if (count && group_count) {
        if (!values) { errno = EFAULT; return -1; }
        memcpy(values, groups, group_count * sizeof(gid_t));
    }
    return group_count;
}

static int capability_words(cap_user_header_t header) {
    if (!header) { errno = EFAULT; return -1; }
    if (header->version == _LINUX_CAPABILITY_VERSION_1) return 1;
    if (header->version == _LINUX_CAPABILITY_VERSION_2 || header->version == _LINUX_CAPABILITY_VERSION_3) return 2;
    header->version = _LINUX_CAPABILITY_VERSION_3;
    errno = EINVAL;
    return -1;
}

int capset(cap_user_header_t header, const cap_user_data_t data) {
    if (!mapped()) return syscall(__NR_capset, header, data);
    int words = capability_words(header);
    if (words < 0) return -1;
    if (header->pid && header->pid != getpid()) { errno = EPERM; return -1; }
    if (!data) { errno = EFAULT; return -1; }
    memset(capabilities, 0, sizeof(capabilities));
    memcpy(capabilities, data, words * sizeof(*data));
    return 0;
}

int capget(cap_user_header_t header, cap_user_data_t data) {
    if (!mapped()) return syscall(__NR_capget, header, data);
    int words = capability_words(header);
    if (words < 0) return -1;
    if (header->pid && header->pid != getpid()) { errno = ESRCH; return -1; }
    if (data) memcpy(data, capabilities, words * sizeof(*data));
    return 0;
}
