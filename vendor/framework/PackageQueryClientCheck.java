package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.lang.reflect.Field;

/** Exercises the same standard Stub client and local reply delegate used by applications. */
final class PackageQueryClientCheck {
    static void run() throws Exception {
        String descriptor = "android.content.pm.IPackageManager";
        Class<?> api = Class.forName(descriptor);
        Class<?> stub = Class.forName(descriptor + "$Stub");
        Field field = stub.getDeclaredField("TRANSACTION_getApplicationInfo");
        field.setAccessible(true);
        int query = field.getInt(null);
        ApplicationInfo original = new ApplicationInfo();
        original.sourceDir = original.publicSourceDir = "/data/app/query-client-check/base.apk";
        Binder remote = new Binder() {
            @Override protected boolean onTransact(int code, Parcel input, Parcel output, int flags) throws RemoteException {
                if (code != query) return super.onTransact(code, input, output, flags);
                input.enforceInterface(descriptor);
                output.writeNoException();
                output.writeInt(1);
                original.writeToParcel(output, 1);
                return true;
            }
        };
        remote.attachInterface(null, descriptor);
        Object raw = stub.getMethod("asInterface", IBinder.class).invoke(null, remote);
        PackageQueryClient.QueryBinder bridge = new PackageQueryClient.QueryBinder(remote);
        Object client = stub.getMethod("asInterface", IBinder.class).invoke(null, bridge);
        if (!client.getClass().getName().equals(descriptor + "$Stub$Proxy"))
            throw new AssertionError("Standard package client changed");
        java.lang.reflect.Method get = api.getMethod("getApplicationInfo", String.class, int.class, int.class);
        ApplicationInfo direct = (ApplicationInfo) get.invoke(raw, "query.client.check", 0, 0);
        ApplicationInfo adapted = (ApplicationInfo) get.invoke(client, "query.client.check", 0, 0);
        if (!original.sourceDir.equals(direct.sourceDir) ||
                !(System.getenv("A10VM_ROOTFS") + original.sourceDir).equals(adapted.sourceDir))
            throw new AssertionError("Direct and adapted package clients changed path semantics");
        if (api.getMethod("asBinder").invoke(raw) != remote)
            throw new AssertionError("Original package Binder replaced");
        System.out.println("code_paths_standard_query_delegate=1");
        System.out.println("code_paths_raw_client_reply=1");
    }
}
