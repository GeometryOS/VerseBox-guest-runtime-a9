package io.a10vm.guest;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLDisplay;
import android.opengl.EGLExt;

public final class EglConfigCheck {
    public static void main(String[] args) {
        EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        int[] version = new int[2], count = new int[1];
        if (!EGL14.eglInitialize(display, version, 0, version, 1))
            throw new AssertionError("EGL initialization failed: " + EGL14.eglGetError());
        boolean nullSelected = false, gles2Selected = false;
        for (int renderable : new int[] {EGLExt.EGL_OPENGL_ES3_BIT_KHR, EGL14.EGL_OPENGL_ES2_BIT}) {
            int[] attributes = {EGL14.EGL_BUFFER_SIZE, 32, EGL14.EGL_ALPHA_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_RENDERABLE_TYPE, renderable, EGL14.EGL_SURFACE_TYPE,
                EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE};
            EGLConfig[] selected = new EGLConfig[1];
            boolean chosen = EGL14.eglChooseConfig(display, attributes, 0, selected, 0, 1, count, 0);
            long handle = selected[0] == null ? 0 : selected[0].getNativeHandle();
            System.out.println("egl_webview_config renderable=" + renderable +
                " chosen=" + chosen + " count=" + count[0] + " handle=" + handle);
            if (chosen && count[0] > 0 && handle == 0) nullSelected = true;
            if (renderable == EGL14.EGL_OPENGL_ES2_BIT && chosen && count[0] > 0 && handle != 0)
                gles2Selected = true;
        }
        if (nullSelected) throw new AssertionError("WebView selected a null EGL configuration handle");
        if (!gles2Selected) throw new AssertionError("WebView GLES2 configuration missing");
        if (!EGL14.eglGetConfigs(display, null, 0, 0, count, 0) || count[0] <= 0)
            throw new AssertionError("No EGL configurations");
        EGLConfig[] all = new EGLConfig[count[0]];
        if (!EGL14.eglGetConfigs(display, all, 0, all.length, count, 0))
            throw new AssertionError("Configuration enumeration failed");
        for (EGLConfig config : all) {
            if (config == null || config.getNativeHandle() == 0)
                throw new AssertionError("Enumeration returned a null EGL configuration handle");
        }
        EGL14.eglTerminate(display);
        System.out.println("egl_config_non_null_handles=1");
    }
}
