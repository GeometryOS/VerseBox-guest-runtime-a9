package io.a10vm.guest;

import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import java.lang.reflect.Method;

public final class DesktopCheck {
    public static void main(String[] args) throws Exception {
        if (Build.VERSION.SDK_INT != 29) throw new AssertionError("Wrong guest SDK");
        Method get = Class.forName("android.os.ServiceManager").getMethod("checkService", String.class);
        Method property = Class.forName("android.os.SystemProperties").getMethod("get", String.class);
        Method isUnlocked = Class.forName("android.os.IUserManager").getMethod("isUserUnlocked", int.class);
        Object packages = null;
        Intent home = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
        Method resolve = Class.forName("android.content.pm.IPackageManager").getMethod(
                "resolveIntent", Intent.class, String.class, int.class, int.class);
        String[] names = {"activity", "activity_task", "package", "user", "window", "display", "power"};
        boolean[] alive = new boolean[names.length];
        boolean ready;
        String boot;
        boolean unlocked;
        ResolveInfo resolved;
        long deadline = SystemClock.elapsedRealtime() + 180000;
        do {
            ready = true;
            boot = (String) property.invoke(null, "sys.boot_completed");
            unlocked = false;
            resolved = null;
            if ("1".equals(boot)) {
                for (int i = 0; i < names.length; ++i) {
                    IBinder service = (IBinder) get.invoke(null, names[i]);
                    alive[i] = service != null && service.isBinderAlive();
                    ready &= alive[i];
                }
                Object user = binderInterface(get, "user", "android.os.IUserManager");
                packages = binderInterface(get, "package", "android.content.pm.IPackageManager");
                unlocked = user != null && (Boolean) isUnlocked.invoke(user, 0);
                resolved = packages == null ? null : (ResolveInfo) resolve.invoke(packages, home, null, 0, 0);
            } else ready = false;
            if (ready && "1".equals(boot) && unlocked && resolved != null &&
                    "com.android.launcher3".equals(resolved.activityInfo.packageName)) break;
            SystemClock.sleep(100);
        } while (SystemClock.elapsedRealtime() < deadline);
        for (int i = 0; i < names.length; ++i) {
            IBinder service = (IBinder) get.invoke(null, names[i]);
            alive[i] = service != null && service.isBinderAlive();
            System.out.println("desktop_service_" + names[i] + "=" + (alive[i] ? "1" : "0"));
        }
        System.out.println("desktop_boot_completed=" + boot);
        System.out.println("desktop_user_unlocked=" + unlocked);
        ResolveInfo locked = packages == null ? null : (ResolveInfo) resolve.invoke(packages, home, null, 0xc0000, 0);
        System.out.println("desktop_home_resolved=" + (resolved == null ? "none" : resolved.activityInfo.packageName));
        System.out.println("desktop_home_direct_boot=" + (locked == null ? "none" : locked.activityInfo.packageName));
        ProcessEvidence.report("desktop");
        if (args.length > 0 && "--dump-surfaceflinger".equals(args[0])) {
            IBinder compositor = (IBinder) get.invoke(null, "SurfaceFlinger");
            if (compositor != null) compositor.dump(java.io.FileDescriptor.out, new String[0]);
        }
        if (!ready || !"1".equals(boot)) throw new AssertionError("Guest desktop services still starting");
        if (!unlocked || resolved == null || !"com.android.launcher3".equals(resolved.activityInfo.packageName))
            throw new AssertionError("Guest user and Launcher3 are not ready");
        DesktopHome.show((IBinder) get.invoke(null, "activity_task"), resolved);
        if (java.util.Arrays.asList(args).contains("--dump-window"))
            ((IBinder) get.invoke(null, "window")).dump(java.io.FileDescriptor.out, new String[] {"windows"});
        System.out.println("desktop_services_ready=1");
    }

    private static Object binderInterface(Method get, String service, String type) throws Exception {
        return Class.forName(type + "$Stub").getMethod("asInterface", IBinder.class)
                .invoke(null, get.invoke(null, service));
    }
}
