package io.a10vm.guest;

import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

@SuppressWarnings("deprecation")
public final class GuestCameraCheck {
    private static final class Preview implements AutoCloseable {
        final EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        final EGLContext context;
        final EGLSurface surface;
        final SurfaceTexture texture;
        final int textureId;

        Preview(Handler handler, CountDownLatch frames) {
            int[] version = new int[2], count = new int[1], names = new int[1];
            EGLConfig[] configs = new EGLConfig[1];
            if (!EGL14.eglInitialize(display, version, 0, version, 1) ||
                    !EGL14.eglChooseConfig(display, new int[] {EGL14.EGL_RENDERABLE_TYPE,
                    EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_NONE}, 0, configs, 0, 1, count, 0) || count[0] == 0)
                throw new AssertionError("Guest camera EGL config");
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                    new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
            if (!EGL14.eglMakeCurrent(display, surface, surface, context))
                throw new AssertionError("Guest camera EGL current");
            GLES20.glGenTextures(1, names, 0);
            textureId = names[0];
            texture = new SurfaceTexture(textureId);
            texture.setOnFrameAvailableListener(frame -> { frame.updateTexImage(); frames.countDown(); }, handler);
        }

        @Override public void close() {
            texture.setOnFrameAvailableListener(null);
            texture.release();
            GLES20.glDeleteTextures(1, new int[] {textureId}, 0);
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            EGL14.eglDestroySurface(display, surface);
            EGL14.eglDestroyContext(display, context);
            EGL14.eglReleaseThread();
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("vendor-policy")) {
            if (args.length > 1 && args[1].equals("configure"))
                Class.forName("android.os.SystemProperties").getMethod("set", String.class, String.class)
                        .invoke(null, "log.tag.VENDOR_TAG_NFE_RETHROW", "D");
            if (!android.util.Log.isLoggable("VENDOR_TAG_NFE_RETHROW", android.util.Log.DEBUG))
                throw new AssertionError("MIUI optional vendor tag warning policy missing");
            System.out.println("camera_vendor_warning=1");
            return;
        }
        android.os.Looper.prepareMainLooper();
        Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
        int count = Camera.getNumberOfCameras();
        System.out.println("camera_count=" + count);
        if (count < 1) throw new AssertionError("Guest camera enumeration is empty");
        if (args.length > 0 && args[0].equals("list")) return;
        int id = args.length > 0 ? Integer.parseInt(args[0]) : 0;
        Camera.CameraInfo info = new Camera.CameraInfo();
        Camera.getCameraInfo(id, info);
        System.out.println("camera_facing=" + info.facing);
        System.out.println("camera_orientation=" + info.orientation);
        HandlerThread thread = new HandlerThread("camera-check");
        thread.start();
        AtomicReference<Camera> camera = new AtomicReference<>();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        CountDownLatch opened = new CountDownLatch(1);
        CountDownLatch preview = new CountDownLatch(3);
        CountDownLatch surfaceFrames = new CountDownLatch(3);
        CountDownLatch captured = new CountDownLatch(1);
        AtomicInteger jpegBytes = new AtomicInteger();
        AtomicReference<Preview> output = new AtomicReference<>();
        Handler handler = new Handler(thread.getLooper());
        try {
            handler.post(() -> {
                try {
                    Camera device = Camera.open(id);
                    camera.set(device);
                    Preview consumer = new Preview(handler, surfaceFrames);
                    output.set(consumer);
                    device.setPreviewTexture(consumer.texture);
                    device.setPreviewCallback((bytes, source) -> {
                        if (bytes != null && bytes.length > 0) preview.countDown();
                    });
                    device.setAutoFocusMoveCallback((moving, source) -> {});
                    device.setAutoFocusMoveCallback(null);
                    System.out.println("camera_focus_move_callback=1");
                    device.startPreview();
                } catch (Throwable error) { failure.set(error); }
                finally { opened.countDown(); }
            });
            if (!opened.await(8, TimeUnit.SECONDS)) throw new AssertionError("Guest camera open stalled");
            if (failure.get() != null) throw new AssertionError("Guest camera open failed", failure.get());
            if (!preview.await(8, TimeUnit.SECONDS)) throw new AssertionError("Guest camera preview has no frames");
            System.out.println("camera_preview_frames=3");
            if (!surfaceFrames.await(8, TimeUnit.SECONDS)) throw new AssertionError("Guest camera surface has no frames");
            System.out.println("camera_surface_frames=3");
            handler.post(() -> {
                try {
                    camera.get().takePicture(null, null, (bytes, source) -> {
                        if (bytes != null && bytes.length > 4 && (bytes[0] & 255) == 255 && (bytes[1] & 255) == 216)
                            jpegBytes.set(bytes.length);
                        captured.countDown();
                    });
                } catch (Throwable error) { failure.set(error); captured.countDown(); }
            });
            if (!captured.await(10, TimeUnit.SECONDS) || jpegBytes.get() == 0)
                throw new AssertionError("Guest camera returned no JPEG", failure.get());
            System.out.println("camera_jpeg_bytes=" + jpegBytes.get());
            System.out.println("camera_capture_ready=1");
        } finally {
            CountDownLatch closed = new CountDownLatch(1);
            handler.post(() -> {
                try {
                    if (camera.get() != null) camera.get().release();
                    if (output.get() != null) output.get().close();
                }
                finally { closed.countDown(); }
            });
            closed.await(3, TimeUnit.SECONDS);
            thread.quitSafely();
        }
    }
}
