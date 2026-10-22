#include "HostDeviceTransport.h"
#include <android/hardware/sensors/1.0/ISensors.h>
#include <hidl/HidlTransportSupport.h>
#include <algorithm>
#include <vector>

using namespace android::hardware;
using namespace android::hardware::sensors::V1_0;

namespace {
Result status(int32_t value) {
    if (value == 0) return Result::OK;
    if (value == -13) return Result::PERMISSION_DENIED;
    if (value == -22) return Result::BAD_VALUE;
    return Result::INVALID_OPERATION;
}
class HostSensors final : public ISensors {
    hidl_vec<SensorInfo> sensors;
public:
    bool initialize() {
        HostDeviceSocket socket("sensors", 0x41313053, 1);
        int32_t count;
        if (!socket.valid() || !socket.get(&count) || count < 0 || count > 256) return false;
        sensors.resize(count);
        for (SensorInfo& sensor : sensors) {
            int32_t type, version, flags;
            std::string name, vendor, stringType;
            if (!socket.get(&sensor.sensorHandle) || !socket.get(&type) || !socket.getString(&name) ||
                    !socket.getString(&vendor) || !socket.getString(&stringType) || !socket.get(&version) ||
                    !socket.getFloat(&sensor.maxRange) || !socket.getFloat(&sensor.resolution) ||
                    !socket.getFloat(&sensor.power) || !socket.get(&sensor.minDelay) ||
                    !socket.get(&sensor.maxDelay) || !socket.get(&flags)) return false;
            sensor.name = name; sensor.vendor = vendor; sensor.typeAsString = stringType;
            sensor.type = static_cast<SensorType>(type); sensor.version = version;
            sensor.flags = flags; sensor.requiredPermission = "";
            sensor.fifoReservedEventCount = 0; sensor.fifoMaxEventCount = 0;
        }
        return true;
    }
    Return<void> getSensorsList(getSensorsList_cb callback) override { callback(sensors); return Void(); }
    Return<Result> setOperationMode(OperationMode mode) override {
        return mode == OperationMode::NORMAL ? Result::OK : Result::INVALID_OPERATION;
    }
    Return<Result> activate(int32_t handle, bool enabled) override {
        HostDeviceSocket socket("sensors", 0x41313053, 2);
        int32_t value;
        return socket.valid() && socket.put(handle) && socket.put(enabled) && socket.get(&value) ?
                status(value) : Result::INVALID_OPERATION;
    }
    Return<Result> batch(int32_t handle, int64_t period, int64_t latency) override {
        if (period < 0 || latency < 0) return Result::BAD_VALUE;
        HostDeviceSocket socket("sensors", 0x41313053, 3);
        int32_t value;
        return socket.valid() && socket.put(handle) && socket.put64(period) && socket.put64(latency) &&
                socket.get(&value) ? status(value) : Result::INVALID_OPERATION;
    }
    Return<void> poll(int32_t maximum, poll_cb callback) override {
        if (maximum <= 0) { callback(Result::BAD_VALUE, {}, {}); return Void(); }
        HostDeviceSocket socket("sensors", 0x41313053, 4, true);
        int32_t count;
        hidl_vec<Event> events;
        bool complete = socket.valid() && socket.put(std::min(maximum, 256)) &&
                socket.get(&count) && count > 0 && count <= maximum && count <= 256;
        if (complete) events.resize(count);
        for (size_t index = 0; complete && index < events.size(); index++) {
            Event& event = events[index];
            int32_t type, accuracy, values;
            complete = socket.get64(&event.timestamp) && socket.get(&event.sensorHandle) &&
                    socket.get(&type) && socket.get(&accuracy) && socket.get(&values) && values >= 0 && values <= 16;
            if (!complete) break;
            event.sensorType = static_cast<SensorType>(type);
            for (int i = 0; complete && i < values; i++) complete = socket.getFloat(&event.u.data[i]);
            if (type == 0) {
                event.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
            }
            else if (type == 1 || type == 2 || type == 3 || type == 4 || type == 9 || type == 10)
                event.u.vec3.status = static_cast<SensorStatus>(accuracy);
        }
        callback(complete ? Result::OK : Result::INVALID_OPERATION, complete ? events : hidl_vec<Event>{}, {});
        return Void();
    }
    Return<Result> flush(int32_t handle) override {
        HostDeviceSocket socket("sensors", 0x41313053, 5);
        int32_t value;
        return socket.valid() && socket.put(handle) && socket.get(&value) ? status(value) : Result::INVALID_OPERATION;
    }
    Return<Result> injectSensorData(const Event&) override { return Result::INVALID_OPERATION; }
    Return<void> registerDirectChannel(const SharedMemInfo&, registerDirectChannel_cb callback) override {
        callback(Result::INVALID_OPERATION, -1); return Void();
    }
    Return<Result> unregisterDirectChannel(int32_t) override { return Result::INVALID_OPERATION; }
    Return<void> configDirectReport(int32_t, int32_t, RateLevel, configDirectReport_cb callback) override {
        callback(Result::INVALID_OPERATION, 0); return Void();
    }
};
}

bool registerHostSensors() {
    android::sp<HostSensors> service = new HostSensors;
    return service->initialize() && service->registerAsService() == android::OK;
}
