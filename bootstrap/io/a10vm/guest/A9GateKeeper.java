package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;

/**
 * Minimal gatekeeper endpoint for Android 9.
 *
 * Redroid 9 ships only vendor/lib64/hw/gatekeeper.redroid.so, while
 * HIDL_FETCH_IGatekeeper() looks for gatekeeper.default.so; the real service
 * therefore aborts, and the project deliberately does not start it.  That is
 * fine until the first boot: LockSettingsService$DeviceProvisionedObserver
 * calls IGateKeeperService.reportDeviceSetupComplete() when
 * user_setup_complete changes and does not null-check the interface, so
 * system_server dies with a NullPointerException on the first provision.
 *
 * This registers a no-op binder under the same name the framework resolves.
 * The guest has no lock screen and its keystore already runs on software
 * keymaster, so all gatekeeper operations either succeed as no-ops or report
 * "no secure user".  It is a guest-side endpoint, not a host override of
 * guest state.
 */
public final class A9GateKeeper {
    private static final String NAME = "android.service.gatekeeper.IGateKeeperService";

    private A9GateKeeper() {}

    public static void register() throws Exception {
        IBinder binder = new Binder() {
            @Override
            protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                if (reply != null) {
                    reply.writeNoException();
                    // getSecureUserId(int userId) returns long; report 0 so the
                    // framework treats the device as having no secure user id.
                    if (code == 5) reply.writeLong(0L);
                }
                return true;
            }
        };
        Class<?> manager = Class.forName("android.os.ServiceManager");
        manager.getMethod("addService", String.class, IBinder.class)
                .invoke(null, NAME, binder);
    }
}
