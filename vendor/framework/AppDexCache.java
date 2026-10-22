package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.os.IBinder;
import java.io.File;

final class AppDexCache {
    static String directory(String supplied, String processName, int uid) throws Exception {
        if (supplied != null && supplied.startsWith("/"))
            return new File(supplied, "code_cache/a10vm-dex-v1").getPath();
        // Android 10 zygote requests can omit --app-data-dir. Query the actual
        // package instead of guessing a data path from its process name.
        IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class).invoke(null, "package");
        if (binder == null) return null;
        Object packages = Class.forName("android.content.pm.IPackageManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        Class<?> api = Class.forName("android.content.pm.IPackageManager");
        String[] names = (String[]) api.getMethod("getPackagesForUid", int.class).invoke(packages, uid);
        if (names == null || names.length == 0) return null;
        int user = (Integer) Class.forName("android.os.UserHandle").getMethod("getUserId", int.class)
                .invoke(null, uid);
        String base = processName.split(":", 2)[0];
        for (String name : names) {
            ApplicationInfo info = (ApplicationInfo) api.getMethod("getApplicationInfo", String.class,
                    int.class, int.class).invoke(packages, name, 0, user);
            if (info != null && info.uid == uid && info.dataDir != null && info.dataDir.startsWith("/") &&
                    (names.length == 1 || base.equals(name) || base.equals(info.processName)))
                return new File(info.dataDir, "code_cache/a10vm-dex-v1").getPath();
        }
        return null;
    }
}
