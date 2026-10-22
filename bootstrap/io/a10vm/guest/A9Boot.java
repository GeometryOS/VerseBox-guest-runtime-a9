package io.a10vm.guest;

import java.lang.reflect.InvocationTargetException;

/** Android 9 system_server bootstrap: no APEX, API 28 class library. */
public final class A9Boot {
    public static void main(String[] args) throws Throwable {
        if (android.os.Build.VERSION.SDK_INT != 28) throw new AssertionError("Wrong guest SDK");
        System.out.println("desktop_guest_sdk=" + android.os.Build.VERSION.SDK_INT);
        ProcessEvidence.report("desktop");
        configureRuntime(28);
        // Permanent stage-5 evidence: this value is the classloader namespace
        // search path for every System.loadLibrary() call.
        System.out.println("a9_java_library_path=" + System.getProperty("java.library.path", ""));
        DesktopProcesses.start();
        System.out.println("desktop_application_endpoint=1");
        try { GuestMemory.start(); }
        catch (Throwable failure) { System.err.println("a9_memory_unavailable=" + failure); }
        try { PrivateVolumes.register(); }
        catch (Throwable failure) { System.err.println("a9_volumes_unavailable=" + failure); }
        try { GuestCapabilities.configure(); }
        catch (Throwable failure) { System.err.println("a9_capabilities_unavailable=" + failure); }
        try { GuestSetup.start(); }
        catch (Throwable failure) { System.err.println("a9_setup_unavailable=" + failure); }
        // API 28 netd endpoint: android.net.util.NetdService resolves the binder
        // service "netd" (android.net.INetd, AIDL) exactly like API 29 does.
        try {
            GuestNetworks.registerNetworkService();
            System.out.println("a9_netd_binder=registered");
        } catch (Throwable failure) { System.err.println("a9_netd_binder=unavailable:" + failure); }
        // API 28's AudioService spins until "media.audio_policy" is published.
        // The ROM audio binaries are 32-bit, so the API 28 runtime publishes the
        // endpoint from the bootstrap dex (see A9AudioPolicy).
        try {
            A9AudioPolicy.register();
            System.out.println("a9_audio_policy=registered");
        } catch (Throwable failure) { System.err.println("a9_audio_policy=unavailable:" + failure); }
        try {
            Class.forName("com.android.server.SystemServer").getMethod("main", String[].class)
                    .invoke(null, (Object) new String[0]);
        } catch (InvocationTargetException failure) {
            throw failure.getCause();
        }
    }

    private static void configureRuntime(int sdk) throws Exception {
        Class<?> vm = Class.forName("dalvik.system.VMRuntime");
        Object runtime = vm.getMethod("getRuntime").invoke(null);
        vm.getMethod("setTargetSdkVersion", int.class).invoke(runtime, sdk);
        Class.forName("android.graphics.Typeface");
        try { Class.forName("android.security.keystore.AndroidKeyStoreProvider")
                .getMethod("install").invoke(null); }
        catch (ClassNotFoundException | NoSuchMethodException ignored) { }
    }
}
