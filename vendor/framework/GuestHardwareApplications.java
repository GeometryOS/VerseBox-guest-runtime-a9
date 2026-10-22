package io.a10vm.guest;

import android.content.Context;
import android.content.pm.PackageManager;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import org.json.JSONArray;
import org.json.JSONObject;

final class GuestHardwareApplications {
    static void configure(Context context) throws Exception {
        String input = System.getenv("A10VM_HARDWARE_APPLICATIONS");
        if (input == null || input.isEmpty()) return;
        Class<?> api = Class.forName("android.hidl.manager.V1_0.IServiceManager");
        Object manager = api.getMethod("getService").invoke(null);
        Path marker = Paths.get("/data/system/a10vm-hardware-applications.json");
        JSONObject saved = Files.exists(marker)
                ? new JSONObject(new String(Files.readAllBytes(marker), StandardCharsets.UTF_8)) : new JSONObject();
        PackageManager packages = context.getPackageManager();
        JSONArray applications = new JSONArray(input);
        for (int i = 0; i < applications.length(); ++i) {
            JSONObject application = applications.getJSONObject(i);
            String name = application.getString("package");
            try { packages.getApplicationInfo(name, PackageManager.MATCH_DISABLED_COMPONENTS); }
            catch (PackageManager.NameNotFoundException missing) { continue; }
            Object service = api.getMethod("get", String.class, String.class).invoke(manager,
                    application.getString("interface"), application.getString("instance"));
            int state = packages.getApplicationEnabledSetting(name);
            if (service == null && (state == PackageManager.COMPONENT_ENABLED_STATE_DEFAULT ||
                    state == PackageManager.COMPONENT_ENABLED_STATE_ENABLED)) {
                saved.put(name, state);
                Files.write(marker, saved.toString().getBytes(StandardCharsets.UTF_8));
                packages.setApplicationEnabledSetting(name, PackageManager.COMPONENT_ENABLED_STATE_DISABLED, 0);
                System.out.println("rom_hardware_application=" + name + " available=false");
            } else if (service != null && saved.has(name) && state == PackageManager.COMPONENT_ENABLED_STATE_DISABLED) {
                packages.setApplicationEnabledSetting(name, saved.getInt(name), 0);
                saved.remove(name);
                Files.write(marker, saved.toString().getBytes(StandardCharsets.UTF_8));
                System.out.println("rom_hardware_application=" + name + " available=true");
            }
        }
    }
}
