#include <hidl/HidlTransportSupport.h>
#include <stdio.h>

bool registerHostSensors();
bool registerHostVibrator();
bool registerHostHealth();
bool registerGuestMtdStorage();

int main() {
    android::hardware::configureRpcThreadpool(4, true);
    // Health is the one HAL the API 28 desktop needs before systemReady:
    // BatteryService resolves the health "backup" instance while starting.
    // Sensors, vibrator and MTD are still being ported.  A missing one must
    // not take the whole process down -- that is exactly how A9 ended up
    // running with the ROM healthd and a battery stuck at boot time.
    bool health = registerHostHealth();
    bool sensors = registerHostSensors();
    bool vibrator = registerHostVibrator();
    bool mtd = registerGuestMtdStorage();
    printf("host_devices_health=%d sensors=%d vibrator=%d mtd=%d\n",
            health, sensors, vibrator, mtd);
    fflush(stdout);
    if (!health) return 1;
    printf("host_devices_ready=1\n"); fflush(stdout);
    android::hardware::joinRpcThreadpool();
    return 0;
}
