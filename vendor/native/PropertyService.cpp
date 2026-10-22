#include "PrivateProperties.h"
#include "PropertyProtocol.h"
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

namespace {
volatile sig_atomic_t stopped;
void stop(int) { stopped = 1; }

bool receiveString(int fd, std::string* output, uint32_t limit) {
    uint32_t size;
    if (!property_read(fd, &size, sizeof(size)) || size >= limit) return false;
    output->resize(size);
    return property_read(fd, &(*output)[0], size) && output->find('\0') == std::string::npos;
}

void handle(int fd, PrivateProperties& properties) {
    ucred peer{};
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != getuid()) return;
    timeval timeout{2, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) return;
    uint32_t command, status = 1;
    std::string name, value;
    if (property_read(fd, &command, sizeof(command)) && command == PROPERTY_SET &&
        receiveString(fd, &name, PROPERTY_NAME_LIMIT) && receiveString(fd, &value, PROPERTY_VALUE_LIMIT) &&
        !name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") ==
        std::string::npos && name.compare(0, 4, "ctl.") != 0 && properties.set(name, value)) status = 0;
    property_write(fd, &status, sizeof(status));
}
}

int main(int argc, char** argv) {
    if (argc != 4 || getuid() < 10000 || geteuid() != getuid()) return 2;
    PrivateProperties properties;
    std::ifstream input(argv[2]);
    if (!input || !properties.create(argv[1])) return 1;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto split = line.find('=');
        if (split == std::string::npos || !properties.set(line.substr(0, split), line.substr(split + 1))) return 1;
    }
    if (input.bad() || properties.get("ro.build.version.sdk") != "29") return 1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (argv[3][0] != '/' || strlen(argv[3]) >= sizeof(address.sun_path)) return 2;
    strcpy(address.sun_path, argv[3]);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        chmod(argv[3], 0600) || listen(listener, 16)) { perror("property service"); return 1; }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    printf("property_service_pid=%d\nproperty_service_ready=1\n", getpid());
    fflush(stdout);
    while (!stopped) {
        pollfd pending{listener, POLLIN, 0};
        int ready = poll(&pending, 1, 100);
        if (ready < 0 && errno != EINTR) { perror("property poll"); close(listener); unlink(argv[3]); return 1; }
        if (ready <= 0 || !(pending.revents & POLLIN)) continue;
        int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd >= 0) { handle(fd, properties); close(fd); }
    }
    close(listener);
    unlink(argv[3]);
    return 0;
}
