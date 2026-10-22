package io.a10vm.guest;

import android.content.Context;
import java.lang.reflect.Array;
import java.lang.reflect.Field;

final class GuestWebViewProviders {
    static void configure(Context context) throws Exception {
        String packageName = System.getenv("A10VM_WEBVIEW_PACKAGE");
        if (packageName == null || packageName.isEmpty()) return;
        Class<?> system = Class.forName("com.android.server.webkit.SystemImpl");
        Object instance = system.getMethod("getInstance").invoke(null);
        Field field = system.getDeclaredField("mWebViewProviderPackages");
        field.setAccessible(true);
        Object previous = field.get(instance);
        Class<?> info = previous.getClass().getComponentType();
        Object provider = info.getConstructor(String.class, String.class, boolean.class,
                boolean.class, String[].class).newInstance(packageName, "A10VM WebView", true, false, new String[0]);
        Object providers = Array.newInstance(info, Array.getLength(previous) + 1);
        Array.set(providers, 0, provider);
        System.arraycopy(previous, 0, providers, 1, Array.getLength(previous));
        field.set(instance, providers);
        // The normal system boot phase prepares RELRO after ActivityManager is ready.
        system.getMethod("updateUserSetting", Context.class, String.class).invoke(instance, context, packageName);
        System.out.println("rom_webview_provider_requested=" + packageName);
    }
}
