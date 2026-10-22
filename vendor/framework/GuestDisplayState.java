package io.a10vm.guest;

import android.os.IBinder;
import android.os.SystemClock;

/** Apply the host window's visibility through the guest's own Android power service. */
public final class GuestDisplayState {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected on, off or inspect");
        Class<?> type = Class.forName("android.os.IPowerManager");
        IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class).invoke(null, "power");
        if (binder == null) throw new IllegalStateException("Guest power service missing");
        Object power = Class.forName("android.os.IPowerManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        long now = SystemClock.uptimeMillis();
        if ("on".equals(args[0])) {
            try {
                type.getMethod("wakeUp", long.class, int.class, String.class, String.class)
                        .invoke(power, now, 0, "a10vm:window", "android");
            } catch (NoSuchMethodException api28) {
                // android-9.0.0_r61 IPowerManager.wakeUp(long, String reason, String packageName)
                // gained the leading `int wakeReason` argument in android-10.0.0.
                type.getMethod("wakeUp", long.class, String.class, String.class)
                        .invoke(power, now, "a10vm:window", "android");
            }
        } else if ("off".equals(args[0])) {
            type.getMethod("goToSleep", long.class, int.class, int.class).invoke(power, now, 0, 1);
        } else if (!"inspect".equals(args[0])) {
            throw new IllegalArgumentException("Expected on, off or inspect");
        }
        boolean interactive = (Boolean) type.getMethod("isInteractive").invoke(power);
        System.out.println("guest_display_interactive=" + interactive);
        if (!"inspect".equals(args[0]) && interactive != "on".equals(args[0]))
            throw new AssertionError("Guest display did not change power state");
    }
}
