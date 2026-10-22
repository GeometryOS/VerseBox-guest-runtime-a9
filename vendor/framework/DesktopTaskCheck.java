package io.a10vm.guest;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.pm.ActivityInfo;
import android.os.IBinder;
import android.os.SystemClock;
import java.lang.reflect.Method;
import java.util.List;

public final class DesktopTaskCheck {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected foreground package");
        Method get = Class.forName("android.os.ServiceManager").getMethod("checkService", String.class);
        Object tasks = Class.forName("android.app.IActivityTaskManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, get.invoke(null, "activity_task"));
        Method query = Class.forName("android.app.IActivityTaskManager").getMethod("getTasks", int.class);
        Object packages = Class.forName("android.content.pm.IPackageManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, get.invoke(null, "package"));
        Method activity = Class.forName("android.content.pm.IPackageManager")
                .getMethod("getActivityInfo", ComponentName.class, int.class, int.class);
        IBinder window = (IBinder) get.invoke(null, "window");
        long deadline = SystemClock.elapsedRealtime() + 10000;
        do {
            List<?> list = (List<?>) query.invoke(tasks, 1);
            if (!list.isEmpty()) {
                ActivityManager.RunningTaskInfo task = (ActivityManager.RunningTaskInfo) list.get(0);
                if (task.topActivity != null && args[0].equals(task.topActivity.getPackageName())) {
                    ActivityInfo info = (ActivityInfo) activity.invoke(packages, task.topActivity, 0, 0);
                    ComponentName target = info != null && info.targetActivity != null
                            ? new ComponentName(args[0], info.targetActivity) : task.topActivity;
                    if (task.baseActivity != null) {
                        ActivityInfo base = (ActivityInfo) activity.invoke(packages, task.baseActivity, 0, 0);
                        if (base != null && task.topActivity.getClassName().equals(base.targetActivity)) {
                            target = task.baseActivity;
                        }
                    }
                    if (DesktopHome.drawn(window, task.topActivity) || DesktopHome.drawn(window, target)) {
                        System.out.println("desktop_task_drawn=" + task.topActivity.flattenToShortString());
                        System.out.println("desktop_task_window=" + target.flattenToShortString());
                        System.out.println("desktop_task_ready=1");
                        return;
                    }
                }
            }
            SystemClock.sleep(50);
        } while (SystemClock.elapsedRealtime() < deadline);
        System.out.println("desktop_task_list=" + query.invoke(tasks, 1));
        window.dump(java.io.FileDescriptor.out, new String[] {"windows"});
        throw new AssertionError("Foreground package has not completed drawing: " + args[0]);
    }
}
