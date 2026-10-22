#include "GuestOwnership.h"
#include "GuestPaths.h"
#include <async_safe/log.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LIBC_STATIC
extern "C" ssize_t a10vm_owner_xattr(long number, const void* target, void* owner, size_t size);
#endif

namespace {
constexpr uint32_t magic = 0x41314f57;
constexpr const char* attribute = "user.a10vm.owner";
struct Owner { uint32_t magic, version, uid, gid; };
static_assert(sizeof(Owner) == guestOwnershipSize);

struct CachedOwner {
    dev_t device;
    ino_t inode;
    timespec changed;
    Owner owner;
    bool valid;
    CachedOwner* next;
};
CachedOwner* owners[256]{};
pthread_mutex_t ownerMutex = PTHREAD_MUTEX_INITIALIZER;

void beforeFork() { pthread_mutex_lock(&ownerMutex); }
void afterFork() { pthread_mutex_unlock(&ownerMutex); }
__attribute__((constructor)) void initializeOwnerCache() {
    pthread_atfork(beforeFork, afterFork, afterFork);
}

bool sameFile(const CachedOwner& entry, const struct stat& info) {
    return entry.valid && entry.device == info.st_dev && entry.inode == info.st_ino &&
        entry.changed.tv_sec == info.st_ctim.tv_sec && entry.changed.tv_nsec == info.st_ctim.tv_nsec;
}

CachedOwner*& bucket(const struct stat& info) {
    uint64_t key = static_cast<uint64_t>(info.st_ino) ^ static_cast<uint64_t>(info.st_dev);
    return owners[(key ^ (key >> 32)) % 256];
}

void apply(const Owner& owner, struct stat* info) {
    if (owner.magic == magic && owner.version == 1) {
        info->st_uid = owner.uid;
        info->st_gid = owner.gid;
    }
}

bool cachedOwnership(struct stat* info) {
    pthread_mutex_lock(&ownerMutex);
    for (CachedOwner* entry = bucket(*info); entry; entry = entry->next) {
        if (!sameFile(*entry, *info)) continue;
        apply(entry->owner, info);
        pthread_mutex_unlock(&ownerMutex);
        return true;
    }
    pthread_mutex_unlock(&ownerMutex);
    return false;
}

void cacheOwnership(const struct stat& info, const Owner& owner) {
    pthread_mutex_lock(&ownerMutex);
    CachedOwner*& head = bucket(info);
    CachedOwner* target = head;
    while (target && (target->device != info.st_dev || target->inode != info.st_ino))
        target = target->next;
    if (!target) {
        target = static_cast<CachedOwner*>(malloc(sizeof(CachedOwner)));
        if (!target) { pthread_mutex_unlock(&ownerMutex); return; }
        target->next = head;
        head = target;
    }
    target->device = info.st_dev;
    target->inode = info.st_ino;
    target->changed = info.st_ctim;
    target->owner = owner;
    target->valid = true;
    pthread_mutex_unlock(&ownerMutex);
}

void invalidateOwnership(const struct stat& info) {
    pthread_mutex_lock(&ownerMutex);
    for (CachedOwner* entry = bucket(info); entry; entry = entry->next)
        if (entry->device == info.st_dev && entry->inode == info.st_ino) entry->valid = false;
    pthread_mutex_unlock(&ownerMutex);
}

bool descriptorPath(int fd, char (&path)[PATH_MAX]) {
    char link[64];
    async_safe_format_buffer(link, sizeof(link), "/proc/self/fd/%d", fd);
    ssize_t size = syscall(__NR_readlinkat, AT_FDCWD, link, path, sizeof(path) - 1);
    if (size < 0) return false;
    path[size] = '\0';
    return guestPhysicalPath(path);
}

int assign(const char* path, int fd, uid_t uid, gid_t gid, struct stat info) {
    if (info.st_uid != static_cast<uid_t>(syscall(__NR_getuid))) { errno = EPERM; return -1; }
    guestApplyOwnership(path, fd, &info);
    Owner owner{magic, 1, uid == static_cast<uid_t>(-1) ? info.st_uid : uid,
                          gid == static_cast<gid_t>(-1) ? info.st_gid : gid};
    int result = path ? syscall(__NR_setxattr, path, attribute, &owner, sizeof(owner), 0)
                      : syscall(__NR_fsetxattr, fd, attribute, &owner, sizeof(owner), 0);
    if (result == 0) invalidateOwnership(info);
    return result;
}
}

void guestApplyOwnership(const char* physical, int descriptor, struct stat* info) {
    if (S_ISLNK(info->st_mode) || (physical && !guestPhysicalPath(physical))) return;
    if (cachedOwnership(info)) return;
    int saved = errno;
    Owner owner{};
#ifdef LIBC_STATIC
    ssize_t size = physical ? syscall(__NR_getxattr, physical, attribute, &owner, sizeof(owner))
                            : syscall(__NR_fgetxattr, descriptor, attribute, &owner, sizeof(owner));
#else
    ssize_t size = a10vm_owner_xattr(physical ? __NR_getxattr : __NR_fgetxattr,
        physical ? static_cast<const void*>(physical) : reinterpret_cast<const void*>(descriptor),
        &owner, sizeof(owner));
#endif
    // Loaded libraries are inspected again after Chromium installs its syscall filter.
    // Inode ctime invalidates both ownership records and cached missing attributes.
    if (size >= 0 || errno == ENODATA || errno == ENOTSUP)
        cacheOwnership(*info, size == sizeof(owner) ? owner : Owner{});
    if (size == sizeof(owner)) apply(owner, info);
    errno = saved;
}

int fstat(int fd, struct stat* info) {
    int result = syscall(__NR_fstat, fd, info);
    if (result == 0 && guestRoot() && !cachedOwnership(info)) {
        int saved = errno;
        char path[PATH_MAX];
        if (descriptorPath(fd, path)) guestApplyOwnership(nullptr, fd, info);
        errno = saved;
    }
    return result;
}
__strong_alias(fstat64, fstat);

int fchown(int fd, uid_t uid, gid_t gid) {
    char path[PATH_MAX];
    if (!guestRoot() || !descriptorPath(fd, path)) return syscall(__NR_fchown, fd, uid, gid);
    struct stat info;
    if (syscall(__NR_fstat, fd, &info) < 0) return -1;
    return assign(nullptr, fd, uid, gid, info);
}

int fchownat(int fd, const char* path, uid_t uid, gid_t gid, int flags) {
    char buffer[PATH_MAX];
    const char* physical = guestPath(fd, path, buffer, !(flags & AT_SYMLINK_NOFOLLOW));
    if (!physical) return -1;
    if (!guestPhysicalPath(physical)) return syscall(__NR_fchownat, fd, physical, uid, gid, flags);
    if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
    struct stat info;
    if (syscall(__NR_newfstatat, fd, physical, &info, flags) < 0) return -1;
    if (S_ISLNK(info.st_mode)) { errno = ENOTSUP; return -1; }
    return assign(physical, -1, uid, gid, info);
}

extern "C" int ___fchmodat(int fd, const char* path, mode_t mode) {
    char buffer[PATH_MAX];
    const char* physical = guestPath(fd, path, buffer);
    return physical ? syscall(__NR_fchmodat, fd, physical, mode) : -1;
}
