package io.a10vm.guest;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Arrays;

/** Android 9 app-process entry wrapper (no APEX, no Android 10 bare-dex cache). */
public final class A9GuestApplication {
    public static void main(String[] args) throws Throwable {
        configureRuntime(Integer.parseInt(args[0]));
        String target = args[1];
        if (target.equals("android.webkit.WebViewLibraryLoader$RelroFileCreator")) {
            android.os.Looper.prepareMainLooper();
            Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
        }
        try {
            Method main = Class.forName(target).getDeclaredMethod("main", String[].class);
            main.setAccessible(true);
            main.invoke(null, (Object) Arrays.copyOfRange(args, 2, args.length));
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
