#pragma once
#include <sys/socket.h>

using HostConnectFunction = int (*)(int, const sockaddr*, socklen_t);
void initializeHostConnect(HostConnectFunction raw, HostConnectFunction legacy);
int hostConnect(int fd, const sockaddr* address, socklen_t length);
