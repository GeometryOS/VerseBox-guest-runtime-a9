package io.a10vm.guest;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import java.lang.reflect.Method;

public final class GuestTap {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("Expected guest x and y");
        float x = Float.parseFloat(args[0]), y = Float.parseFloat(args[1]);
        Class<?> api = Class.forName("android.hardware.input.InputManager");
        Object manager = api.getMethod("getInstance").invoke(null);
        Method inject = api.getMethod("injectInputEvent", InputEvent.class, int.class);
        long down = SystemClock.uptimeMillis();
        for (int action : new int[] {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP}) {
            MotionEvent event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, y, 0);
            event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
            try {
                if (!(Boolean) inject.invoke(manager, event, 2))
                    throw new AssertionError("Guest touch injection failed");
            } finally { event.recycle(); }
            if (action == MotionEvent.ACTION_DOWN) SystemClock.sleep(50);
        }
        System.out.println("guest_tap_ready=1");
    }
}
