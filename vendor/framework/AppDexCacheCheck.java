package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.os.IBinder;
import java.io.File;

public final class AppDexCacheCheck {
    public static void main(String[] args) throws Exception {
        if (args.length == 0) throw new IllegalArgumentException("Expected installed package names");
        IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class).invoke(null, "package");
        Object packages = Class.forName("android.content.pm.IPackageManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        Class<?> api = Class.forName("android.content.pm.IPackageManager");
        for (String name : args) {
            ApplicationInfo info = (ApplicationInfo) api.getMethod("getApplicationInfo", String.class,
                    int.class, int.class).invoke(packages, name, 0, 0);
            if (info == null) throw new AssertionError("Package missing: " + name);
            String expected = new File(info.dataDir, "code_cache/a10vm-dex-v1").getPath();
            for (String process : new String[] {name, name + ":cache-check"})
                if (!expected.equals(AppDexCache.directory(null, process, info.uid)))
                    throw new AssertionError("Missing package-owned DEX cache: " + process);
            if (!expected.equals(AppDexCache.directory(info.dataDir, name, info.uid)))
                throw new AssertionError("Supplied application data directory changed");
            System.out.println("app_dex_cache_directory=1 package=" + name);
        }
        if (AppDexCache.directory(null, "cache-check.missing", Integer.MAX_VALUE) != null)
            throw new AssertionError("Missing package received a guessed data directory");
        System.out.println("app_dex_cache_directory_complete=1");
    }
}
