#include "HostDeviceTransport.h"
#include <android/hardware/health/2.0/IHealth.h>
#include <hidl/HidlTransportSupport.h>
#include <algorithm>
#include <limits>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

using android::hardware::Return;
using android::hardware::Void;
using android::hardware::health::V1_0::BatteryHealth;
using android::hardware::health::V1_0::BatteryStatus;
using namespace android::hardware::health::V2_0;

namespace {
struct Battery {
    HealthInfo info{};
    int32_t currentAverage = std::numeric_limits<int32_t>::min();
    int64_t energy = std::numeric_limits<int64_t>::min();
};

bool readBattery(Battery* battery) {
    HostDeviceSocket socket("battery", 0x41313042, 1);
    int32_t result, plugged, status, health, present;
    std::string technology;
    auto& info = battery->info.legacy;
    if (!socket.valid() || !socket.get(&result) || result || !socket.get(&plugged) ||
            !socket.get(&status) || !socket.get(&health) || !socket.get(&present) ||
            !socket.get(&info.batteryLevel) || !socket.get(&info.batteryVoltage) ||
            !socket.get(&info.batteryTemperature) || !socket.getString(&technology) ||
            !socket.get(&info.batteryChargeCounter) || !socket.get(&info.batteryCurrent) ||
            !socket.get(&battery->currentAverage) || !socket.get64(&battery->energy)) return false;
    info.chargerAcOnline = plugged & 1;
    info.chargerUsbOnline = plugged & 2;
    info.chargerWirelessOnline = plugged & 4;
    info.batteryStatus = static_cast<BatteryStatus>(status);
    info.batteryHealth = static_cast<BatteryHealth>(health);
    info.batteryPresent = present != 0;
    info.batteryTechnology = technology;
    return true;
}

class HostHealth final : public IHealth {
    std::mutex lock;
    std::vector<android::sp<IHealthInfoCallback>> callbacks;

    template<class Callback> Return<void> property(Callback callback, int field) {
        Battery battery;
        if (!readBattery(&battery)) { callback(Result::UNKNOWN, 0); return Void(); }
        int32_t value = field == 0 ? battery.info.legacy.batteryChargeCounter :
                field == 1 ? battery.info.legacy.batteryCurrent :
                field == 2 ? battery.currentAverage : battery.info.legacy.batteryLevel;
        bool supported = value != std::numeric_limits<int32_t>::min();
        callback(supported ? Result::SUCCESS : Result::NOT_SUPPORTED, supported ? value : 0);
        return Void();
    }
public:
    Return<Result> registerCallback(const android::sp<IHealthInfoCallback>& callback) override {
        if (!callback) return Result::UNKNOWN;
        {
            std::lock_guard<std::mutex> guard(lock);
            bool found = std::any_of(callbacks.begin(), callbacks.end(), [&](const auto& item) {
                return android::hardware::interfacesEqual(item, callback);
            });
            if (!found) callbacks.push_back(callback);
        }
        return update();
    }
    Return<Result> unregisterCallback(const android::sp<IHealthInfoCallback>& callback) override {
        std::lock_guard<std::mutex> guard(lock);
        auto at = std::find_if(callbacks.begin(), callbacks.end(), [&](const auto& item) {
            return android::hardware::interfacesEqual(item, callback);
        });
        if (at == callbacks.end()) return Result::NOT_FOUND;
        callbacks.erase(at); return Result::SUCCESS;
    }
    Return<Result> update() override {
        Battery battery;
        if (!readBattery(&battery)) return Result::UNKNOWN;
        std::vector<android::sp<IHealthInfoCallback>> targets;
        { std::lock_guard<std::mutex> guard(lock); targets = callbacks; }
        Result result = Result::SUCCESS;
        for (const auto& callback : targets) {
            if (!callback->healthInfoChanged(battery.info).isOk()) {
                unregisterCallback(callback);
                result = Result::CALLBACK_DIED;
            }
        }
        return result;
    }
    Return<void> getChargeCounter(getChargeCounter_cb callback) override { return property(callback, 0); }
    Return<void> getCurrentNow(getCurrentNow_cb callback) override { return property(callback, 1); }
    Return<void> getCurrentAverage(getCurrentAverage_cb callback) override { return property(callback, 2); }
    Return<void> getCapacity(getCapacity_cb callback) override { return property(callback, 3); }
    Return<void> getEnergyCounter(getEnergyCounter_cb callback) override {
        Battery battery;
        bool ready = readBattery(&battery);
        bool supported = battery.energy != std::numeric_limits<int64_t>::min();
        callback(!ready ? Result::UNKNOWN : supported ? Result::SUCCESS : Result::NOT_SUPPORTED,
                 ready && supported ? battery.energy : 0);
        return Void();
    }
    Return<void> getChargeStatus(getChargeStatus_cb callback) override {
        Battery battery;
        bool ready = readBattery(&battery);
        callback(ready ? Result::SUCCESS : Result::UNKNOWN,
                 ready ? battery.info.legacy.batteryStatus : BatteryStatus::UNKNOWN);
        return Void();
    }
    Return<void> getStorageInfo(getStorageInfo_cb callback) override {
        callback(Result::NOT_SUPPORTED, {}); return Void();
    }
    Return<void> getDiskStats(getDiskStats_cb callback) override {
        callback(Result::NOT_SUPPORTED, {}); return Void();
    }
    Return<void> getHealthInfo(getHealthInfo_cb callback) override {
        Battery battery;
        bool ready = readBattery(&battery);
        callback(ready ? Result::SUCCESS : Result::UNKNOWN, battery.info); return Void();
    }
};
}

bool registerHostHealth() {
    Battery battery;
    if (!readBattery(&battery)) return false;
    android::sp<HostHealth> service = new HostHealth;
    if (service->registerAsService() != android::OK) return false;
    std::thread([service] {
        while (true) { sleep(10); service->update().isOk(); }
    }).detach();
    return true;
}
