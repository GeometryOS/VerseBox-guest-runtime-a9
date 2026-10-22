package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.VersionedPackage;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.concurrent.TimeUnit;

public final class GuestPackageQueryCheck {
    public static void main(String[] args) throws Exception {
        GuestApplication.configureRuntime(29);
        if (android.os.Process.isApplicationUid(android.os.Process.myUid())) PackageQueryClient.install();
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Object manager = thread.getDeclaredMethod("getPackageManager").invoke(null);
        Field field = thread.getDeclaredField("sPackageManager");
        field.setAccessible(true);
        System.out.println("package_query_client=" + manager.getClass().getName());
        if (Proxy.isProxyClass(manager.getClass())) throw new AssertionError("Dynamic package-manager proxy installed");
        Class<?> service = Class.forName("android.content.pm.IPackageManager");
        ApplicationInfo application = (ApplicationInfo) service.getMethod("getApplicationInfo", String.class, int.class, int.class)
                .invoke(manager, args[0], 0, 0);
        PackageInfo info = (PackageInfo) service.getMethod("getPackageInfo", String.class, int.class, int.class)
                .invoke(manager, args[0], 0, 0);
        if (application == null || info == null || info.applicationInfo == null)
            throw new AssertionError("Package query returned no application");
        PackageInfo versioned = (PackageInfo) service.getMethod("getPackageInfoVersioned",
                VersionedPackage.class, int.class, int.class).invoke(manager,
                new VersionedPackage(args[0], info.getLongVersionCode()), 0, 0);
        String root = System.getenv("A10VM_ROOTFS");
        boolean applicationCaller = android.os.Process.isApplicationUid(android.os.Process.myUid());
        String prefix = applicationCaller ? root + "/data/app/" : "/data/app/";
        if (!application.sourceDir.startsWith(prefix) ||
                !application.sourceDir.equals(info.applicationInfo.sourceDir))
            throw new AssertionError("Package query changed caller path semantics: " + prefix);
        if (versioned == null || versioned.applicationInfo == null ||
                !application.sourceDir.equals(versioned.applicationInfo.sourceDir))
            throw new AssertionError("Versioned package query returned a different APK path");
        System.out.println("package_query_versioned_paths_consistent=1");
        System.out.println("package_query_path_space=" + (applicationCaller ? "application" : "system"));
        if (field.get(null) != manager) throw new AssertionError("Package-manager identity changed");
        if (args.length > 1 && applicationCaller) {
            ProcessBuilder reader = new ProcessBuilder(System.getenv("A10VM_EXEC_LINKER"), args[1], application.sourceDir)
                    .inheritIO();
            reader.environment().remove("LD_PRELOAD");
            Process process = reader.start();
            if (!process.waitFor(3, TimeUnit.SECONDS) || process.exitValue() != 0) {
                process.destroyForcibly();
                throw new AssertionError("Direct SVC rejected the queried APK");
            }
        }
        System.out.println("package_query_standard_client=1");
        System.out.println("package_query_paths_consistent=1");
        System.out.println("package_query_complete=1");
    }
}
