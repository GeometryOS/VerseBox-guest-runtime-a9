package io.a10vm.guest;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.util.DisplayMetrics;
import android.view.Display;

public final class DisplayModeCheck {
    public static void main(String[] args) throws Exception {
        android.os.Looper.prepareMainLooper();
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Context context = (Context) thread.getMethod("getSystemContext")
                .invoke(thread.getMethod("systemMain").invoke(null));
        Display display = context.getSystemService(DisplayManager.class).getDisplay(Display.DEFAULT_DISPLAY);
        Display.Mode mode = display.getMode();
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);
        System.out.println("display_size=" + mode.getPhysicalWidth() + "x" + mode.getPhysicalHeight());
        System.out.println("display_refresh_hz=" + mode.getRefreshRate());
        System.out.println("display_density=" + metrics.densityDpi);
        if (mode.getPhysicalWidth() != 1080 || mode.getPhysicalHeight() != 2160 ||
                Math.abs(mode.getRefreshRate() - 120f) > .01f || metrics.densityDpi != 480)
            throw new AssertionError("Expected guest 1080p at 120 Hz and 480 dpi");
        System.out.println("display_mode_ready=1");
    }
}
