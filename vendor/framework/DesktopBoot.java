package io.a10vm.guest;

import android.os.Build;
import java.lang.reflect.InvocationTargetException;

public final class DesktopBoot {
    public static void main(String[] args) throws Throwable {
        if (Build.VERSION.SDK_INT != 29) throw new AssertionError("Wrong guest SDK");
        System.out.println("desktop_guest_sdk=" + Build.VERSION.SDK_INT);
        ProcessEvidence.report("desktop");
        GuestApplication.configureRuntime(29);
        FlattenedApex.register();
        DesktopProcesses.start();
        GuestMemory.start();
        PrivateVolumes.register();
        PrivateKeyStore.register();
        GuestCredentialCapabilities.register();
        GuestNetworks.register();
        GuestConnectivity.start();
        GuestCapabilities.configure();
        GuestSetup.start();
        try {
            Class.forName("com.android.server.SystemServer").getMethod("main", String[].class)
                    .invoke(null, (Object) new String[0]);
        } catch (InvocationTargetException failure) {
            throw failure.getCause();
        }
    }
}
