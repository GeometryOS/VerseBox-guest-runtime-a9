package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import java.lang.reflect.Method;

public final class WifiDeveloperCheck {
    public static void main(String[] args) {
        try { verify(); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(1); }
    }

    private static void verify() throws Exception {
        Class<?> api = Class.forName("android.net.wifi.IWifiManager");
        Method query, update;
        try {
            query = api.getMethod("isWifiCoverageExtendFeatureEnabled");
            update = api.getMethod("enableWifiCoverageExtendFeature", boolean.class);
        } catch (NoSuchMethodException absent) {
            System.out.println("wifi_developer_extension=absent");
            return;
        }
        Binder endpoint = LocalGuestService.endpoint(api.getName(), new GuestWifi());
        Binder remote = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws android.os.RemoteException {
                return endpoint.transact(code, data, reply, flags);
            }
        };
        Object service = Class.forName(api.getName() + "$Stub").getMethod("asInterface", IBinder.class)
                .invoke(null, remote);
        if (!Boolean.FALSE.equals(query.invoke(service))) throw new AssertionError("Relay enabled RF extension");
        for (boolean requested : new boolean[] {true, false}) {
            update.invoke(service, requested);
            if (!Boolean.FALSE.equals(query.invoke(service)))
                throw new AssertionError("Relay advertised an unavailable RF extension");
        }
        System.out.println("wifi_developer_extension=disabled");
        System.out.println("wifi_developer_binder_complete=1");
    }
}
