#include "HostDeviceTransport.h"
#include <android/hardware/vibrator/1.0/IVibrator.h>

using android::hardware::Return;
using android::hardware::Void;
using namespace android::hardware::vibrator::V1_0;

namespace {
int request(uint32_t command, uint32_t argument = 0) {
    HostDeviceSocket socket("vibrator", 0x41313056, command);
    int32_t value;
    if (!socket.valid() || ((command == 3 || command == 5) && !socket.put(argument)) || !socket.get(&value))
        return -19;
    return value;
}
Status status(int value) {
    return value == 0 ? Status::OK : value == -22 ? Status::BAD_VALUE : Status::UNSUPPORTED_OPERATION;
}
class HostVibrator final : public IVibrator {
    Return<Status> on(uint32_t millis) override { return status(request(3, millis)); }
    Return<Status> off() override { return status(request(4)); }
    Return<bool> supportsAmplitudeControl() override { return request(2) == 1; }
    Return<Status> setAmplitude(uint8_t amplitude) override { return status(request(5, amplitude)); }
    Return<void> perform(Effect, EffectStrength, perform_cb callback) override {
        callback(Status::UNSUPPORTED_OPERATION, 0); return Void();
    }
};
}

bool registerHostVibrator() {
    if (request(1) != 1) return true;
    android::sp<HostVibrator> service = new HostVibrator;
    return service->registerAsService() == android::OK;
}
