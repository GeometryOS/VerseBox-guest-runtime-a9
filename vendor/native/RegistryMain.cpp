#include "ServiceRegistry.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/system_properties.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2 || getuid() < 10000 || geteuid() != getuid()) return 2;
    if (setenv("A10VM_BINDER_PATH", argv[1], 1)) return 1;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    char sdk[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.version.sdk", sdk);
    printf("binder_pid=%d\nbinder_uid=%u\nbinder_sdk=%s\n", getpid(), getuid(), sdk);
    return a10binder::runServiceRegistry();
}
