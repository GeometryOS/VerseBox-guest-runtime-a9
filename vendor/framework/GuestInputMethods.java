package io.a10vm.guest;

import android.content.ContentResolver;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.inputmethod.InputMethodInfo;
import android.view.inputmethod.InputMethodManager;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

final class GuestInputMethods {
    private static final String PACKAGE = "com.android.inputmethod.latin";

    static void configure(Context context, boolean firstSetup, long deadline) throws Exception {
        try { context.getPackageManager().getApplicationInfo(PACKAGE, 0); }
        catch (PackageManager.NameNotFoundException missing) { return; }
        java.lang.reflect.Method service = Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class);
        while (service.invoke(null, "input_method") == null) {
            if (SystemClock.elapsedRealtime() >= deadline)
                throw new IllegalStateException("Input method service was not registered");
            SystemClock.sleep(100);
        }
        InputMethodManager manager = context.getSystemService(InputMethodManager.class);
        List<InputMethodInfo> methods;
        InputMethodInfo bundled;
        do {
            methods = manager.getInputMethodList();
            bundled = null;
            for (InputMethodInfo method : methods)
                if (PACKAGE.equals(method.getPackageName())) bundled = method;
            if (bundled != null) break;
            if (SystemClock.elapsedRealtime() >= deadline)
                throw new IllegalStateException("Bundled keyboard was not registered");
            SystemClock.sleep(100);
        } while (true);

        ContentResolver resolver = context.getContentResolver();
        String current = Settings.Secure.getString(resolver, Settings.Secure.DEFAULT_INPUT_METHOD);
        String enabled = Settings.Secure.getString(resolver, Settings.Secure.ENABLED_INPUT_METHODS);
        List<String> entries = new ArrayList<>();
        if (enabled != null && !enabled.isEmpty()) entries.addAll(Arrays.asList(enabled.split(":")));
        boolean present = false;
        for (String entry : entries) if (entry.split(";", 2)[0].equals(bundled.getId())) present = true;
        if (!present) {
            entries.add(bundled.getId());
            if (!Settings.Secure.putString(resolver, Settings.Secure.ENABLED_INPUT_METHODS, String.join(":", entries)))
                throw new IllegalStateException("Enable bundled keyboard");
        }
        boolean valid = false;
        for (InputMethodInfo method : methods) {
            if (!method.getId().equals(current)) continue;
            for (String entry : entries) if (entry.split(";", 2)[0].equals(current)) valid = true;
        }
        if (firstSetup || !valid) {
            if (!Settings.Secure.putString(resolver, Settings.Secure.DEFAULT_INPUT_METHOD, bundled.getId()))
                throw new IllegalStateException("Select bundled keyboard");
            Settings.Secure.putInt(resolver, Settings.Secure.SELECTED_INPUT_METHOD_SUBTYPE, -1);
        }
        System.out.println("guest_keyboard_ready=" + bundled.getId() + " default=" + (firstSetup || !valid));
    }
}
