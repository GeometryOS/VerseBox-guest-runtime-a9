// Minimal native daemon socket end for the API 28 private runtime.
//
// Two API 28 clients block on these sockets:
//   * NetworkManagementService.create() waits for a connectable
//     /dev/socket/netd (NativeDaemonConnector), and its systemReady() sends
//     "firewall enable blacklist" then waits for a reply.  A listener that only
//     accepts makes that command time out after 120 s and kills the system
//     server (NativeDaemonTimeoutException).
//   * NsdService.create() waits for /dev/socket/mdns the same way.
//
// Android 10 dropped the socket wait (GuestNetworks registers the binder
// endpoint) and provides mdns from GuestDiscovery, so only the API 28 runtime
// needs this program.  The guest connects to /dev/socket/<name>, which the path
// layer resolves to <rootfs>/dev/socket/<name>; the path is passed in explicitly
// instead of going through GuestPaths.
//
// Replies follow the framework's NativeDaemonConnector protocol:
//   request:  "<sequence> <command...>\0"
//   response: "<code> <message>\0", where 200 means success.
// The API 28 runtime has no kernel rule tables, so every command is accepted
// with 200 instead of being executed.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void serve(int client) {
    char buffer[4096];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(client, buffer + used, sizeof(buffer) - used);
        if (count <= 0) return;
        used += (size_t)count;
        size_t consumed = 0;
        for (;;) {
            char* end = memchr(buffer + consumed, '\0', used - consumed);
            if (end == NULL) break;
            // One command: "<sequence> <command...>".  The framework matches the
            // reply by sequence, so it must be echoed back.
            int sequence = 0;
            if (sscanf(buffer + consumed, "%d", &sequence) != 1) sequence = 0;
            char reply[64];
            int length = snprintf(reply, sizeof(reply), "200 %d Ok", sequence);
            if (length <= 0 || write(client, reply, (size_t)length) < 0) return;
            if (write(client, "\0", 1) < 0) return;
            consumed = (size_t)(end - buffer) + 1;
        }
        if (consumed > 0) {
            memmove(buffer, buffer + consumed, used - consumed);
            used -= consumed;
        }
        if (used == sizeof(buffer)) used = 0;  // malformed stream: drop it
    }
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/dev/socket/netd";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "netd_listener_error=socket:%s\n", strerror(errno));
        return 1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "netd_listener_error=path-too-long\n");
        return 1;
    }
    strcpy(address.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fprintf(stderr, "netd_listener_error=bind:%s:%s\n", path, strerror(errno));
        return 1;
    }
    if (listen(fd, 8) < 0) {
        fprintf(stderr, "netd_listener_error=listen:%s\n", strerror(errno));
        return 1;
    }
    printf("netd_listener_ready=1 path=%s\n", path);
    fflush(stdout);
    for (;;) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "netd_listener_error=accept:%s\n", strerror(errno));
            return 1;
        }
        serve(client);
        close(client);
    }
}
