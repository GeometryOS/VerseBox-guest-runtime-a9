package io.a10vm.guest;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.os.IBinder;
import android.os.SystemClock;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

final class DesktopHome {
    static void show(IBinder binder, ResolveInfo home) throws Exception {
        Class<?> type = Class.forName("android.app.IActivityTaskManager");
        Object tasks = Class.forName("android.app.IActivityTaskManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        ComponentName component = new ComponentName(home.activityInfo.packageName, home.activityInfo.name);
        Intent intent = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME)
                .setComponent(component).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        Method launch = type.getMethod("startActivityAndWait", Class.forName("android.app.IApplicationThread"),
                String.class, Intent.class, String.class, IBinder.class, String.class, int.class, int.class,
                Class.forName("android.app.ProfilerInfo"), android.os.Bundle.class, int.class);
        Object result = launch.invoke(tasks, null, "android", intent, null, null, null, 0, 0, null, null, 0);
        System.out.println("desktop_home_launch=" + result.getClass().getField("result").get(result));
        System.out.println("desktop_home_launch_timeout=" + result.getClass().getField("timeout").get(result));
        Method query = type.getMethod("getTasks", int.class);
        IBinder window = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class).invoke(null, "window");
        long deadline = SystemClock.elapsedRealtime() + 15000;
        do {
            List<?> list = (List<?>) query.invoke(tasks, 1);
            if (!list.isEmpty()) {
                ActivityManager.RunningTaskInfo task = (ActivityManager.RunningTaskInfo) list.get(0);
                if (component.equals(task.topActivity) && drawn(window, component)) {
                    System.out.println("desktop_home_visible=" + component.flattenToShortString());
                    System.out.println("desktop_home_drawn=1");
                    return;
                }
            }
            SystemClock.sleep(50);
        } while (SystemClock.elapsedRealtime() < deadline);
        System.out.println("desktop_home_tasks=" + query.invoke(tasks, 1));
        window.dump(java.io.FileDescriptor.out, new String[] {"windows"});
        throw new AssertionError("Launcher3 foreground window has not completed drawing");
    }

    static boolean drawn(IBinder window, ComponentName component) throws Exception {
        File state = File.createTempFile("desktop-window-", ".txt", new File("/data/local/tmp"));
        try {
            try (FileOutputStream output = new FileOutputStream(state)) {
                window.dump(output.getFD(), new String[] {"windows"});
            }
            String text = new String(Files.readAllBytes(state.toPath()), StandardCharsets.UTF_8);
            for (String block : text.split("(?=  Window #)")) {
                int end = block.indexOf('\n');
                if (end > 0 && (block.substring(0, end).contains(component.flattenToString()) ||
                        block.substring(0, end).contains(component.flattenToShortString())) &&
                        block.contains("mDrawState=HAS_DRAWN") && block.contains("mHasSurface=true") &&
                        block.contains("isVisible=true")) return true;
            }
            return false;
        } finally { Files.deleteIfExists(state.toPath()); }
    }
}
