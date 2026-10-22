package io.a10vm.guest;

import android.os.Binder;

final class GuestOemNetd {
    static Binder create() throws Exception {
        String descriptor = "com.android.internal.net.IOemNetd";
        try {
            Class.forName(descriptor);
        } catch (ClassNotFoundException absent) {
            return null;
        }
        return LocalGuestService.endpoint(descriptor, (proxy, method, args) -> {
            if ("isAlive".equals(method.getName())) return true;
            if ("registerOemUnsolicitedEventListener".equals(method.getName()) ||
                    "setPidForPackage".equals(method.getName())) return null;
            if (method.getReturnType() == boolean.class) return false;
            throw GuestNetworks.error(95, "Guest OEM network operation: " + method.getName());
        });
    }
}
