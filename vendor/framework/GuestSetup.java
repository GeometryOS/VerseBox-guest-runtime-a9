package io.a10vm.guest;

import android.content.ContentResolver;
import android.content.Context;
import android.os.SystemClock;
import android.provider.Settings;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

final class GuestSetup {
    static void start() {
        new Thread(() -> {
            try { configure(); }
            catch (Exception error) { throw new IllegalStateException("Guest first setup failed", error); }
        }, "guest-first-setup").start();
    }

    private static void configure() throws Exception {
        Path marker = Paths.get("/data/system/a10vm-setup.complete");
        Method service = Class.forName("android.os.ServiceManager").getMethod("checkService", String.class);
        long timeout = Long.getLong("a10vm.bootSetupTimeoutMs", 15000L);
        if (timeout <= 0) throw new IllegalArgumentException("Guest setup timeout must be positive");
        long deadline = SystemClock.elapsedRealtime() + timeout;
        while (service.invoke(null, "lock_settings") == null) {
            if (SystemClock.elapsedRealtime() >= deadline)
                throw new IllegalStateException("Guest settings provider did not start");
            SystemClock.sleep(10);
        }
        Class<?> activity = Class.forName("android.app.ActivityThread");
        Object thread = activity.getMethod("currentActivityThread").invoke(null);
        Context context = (Context) activity.getMethod("getSystemContext").invoke(thread);
        GuestHardwareApplications.configure(context);
        GuestWebViewProviders.configure(context);
        context.getPackageManager().setComponentEnabledSetting(new android.content.ComponentName(
                "com.android.settings", "com.android.settings.Settings$WifiSettingsActivity"),
                android.content.pm.PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
                android.content.pm.PackageManager.DONT_KILL_APP);
        GuestInputMethods.configure(context, !Files.exists(marker), deadline);
        if (Files.exists(marker)) return;
        ContentResolver resolver = context.getContentResolver();
        if (Settings.Secure.getString(resolver, "sysui_tuner_version") == null &&
                !Settings.Secure.putInt(resolver, "sysui_tuner_version", 4))
            throw new IllegalStateException("Guest SystemUI initial settings");
        if (!Settings.Secure.putInt(resolver, "user_setup_complete", 1) ||
                !Settings.Global.putInt(resolver, "device_provisioned", 1))
            throw new IllegalStateException("Guest device provisioning");
        Files.createFile(marker);
        System.out.println("guest_setup_completed=1");
    }
}
