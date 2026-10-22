#include "PropertyProtocol.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/un.h>

int __system_property_set(const char* key, const char* value) {
    const char* path = getenv("A10VM_PROPERTY_SOCKET");
    if (!key || !path || path[0] != '/') { errno = ENOTCONN; return -1; }
    if (!value) value = "";
    uint32_t name_size = strlen(key), value_size = strlen(value), command = PROPERTY_SET, status = 1;
    if (!name_size || name_size >= PROPERTY_NAME_LIMIT || value_size >= PROPERTY_VALUE_LIMIT) {
        errno = EINVAL;
        return -1;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path)) { errno = ENAMETOOLONG; return -1; }
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 2};
    int connected = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
                    connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0;
    int ok = connected && property_write(fd, &command, sizeof(command)) &&
             property_write(fd, &name_size, sizeof(name_size)) && property_write(fd, key, name_size) &&
             property_write(fd, &value_size, sizeof(value_size)) && property_write(fd, value, value_size) &&
             property_read(fd, &status, sizeof(status));
    int error = ok ? (status ? EACCES : 0) : (errno ? errno : EIO);
    close(fd);
    errno = error;
    return error ? -1 : 0;
}
