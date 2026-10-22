#include <hidl/HidlTransportSupport.h>
#include <stdio.h>

bool registerHostSensors();
bool registerHostVibrator();
bool registerHostHealth();
bool registerGuestMtdStorage();

int main() {
    android::hardware::configureRpcThreadpool(4, true);
    if (!registerHostSensors() || !registerHostVibrator() || !registerHostHealth() ||
            !registerGuestMtdStorage()) return 1;
    printf("host_devices_ready=1\n"); fflush(stdout);
    android::hardware::joinRpcThreadpool();
    return 0;
}
