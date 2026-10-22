#include "ServiceRegistry.h"
#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <map>
#include <stdio.h>

namespace android { void setTheContextObject(sp<BBinder> object); }

namespace a10binder {
using namespace android;
class ServiceRegistry final : public BBinder {
    std::map<String16, sp<IBinder>> services;
    status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t) override {
        if (code < IBinder::FIRST_CALL_TRANSACTION || code > IBinder::LAST_CALL_TRANSACTION)
            return BBinder::onTransact(code, data, reply);
        if (!data.enforceInterface(IServiceManager::descriptor)) return PERMISSION_DENIED;
        switch (code) {
            case IServiceManager::ADD_SERVICE_TRANSACTION: {
                String16 name = data.readString16();
                sp<IBinder> object = data.readStrongBinder();
                data.readInt32(); data.readInt32();
                if (object == nullptr) return BAD_VALUE;
                services[name] = object;
                reply->writeInt32(0);
                return OK;
            }
            case IServiceManager::GET_SERVICE_TRANSACTION:
            case IServiceManager::CHECK_SERVICE_TRANSACTION: {
                auto found = services.find(data.readString16());
                return reply->writeStrongBinder(found == services.end() ? nullptr : found->second);
            }
            case IServiceManager::LIST_SERVICES_TRANSACTION: {
                int index = data.readInt32(); data.readInt32();
                if (index < 0 || size_t(index) >= services.size()) return BAD_VALUE;
                auto it = services.begin(); std::advance(it, index);
                return reply->writeString16(it->first);
            }
            default: return UNKNOWN_TRANSACTION;
        }
    }
};

int runServiceRegistry() {
    sp<ProcessState> process = ProcessState::self();
    process->setThreadPoolMaxThreadCount(0);
    sp<BBinder> object = new ServiceRegistry;
    process->setContextObject(object);
    setTheContextObject(object);
    bool ready = process->becomeContextManager(nullptr, nullptr);
    printf("CHECK\tregistry_context\t%d\n", ready);
    if (!ready) return 3;
    printf("binder_registry_ready=1\n");
    IPCThreadState::self()->joinThreadPool();
    printf("binder_registry_returned=1\n");
    return 0;
}
}
