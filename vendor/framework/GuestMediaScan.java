package io.a10vm.guest;

import android.content.Context;
import android.media.MediaScannerConnection;
import android.os.Looper;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class GuestMediaScan {
    public static void main(String[] args) throws Exception {
        if (args.length == 0) throw new IllegalArgumentException("Media paths required");
        ProcessEvidence.report("media_scan");
        Looper.prepareMainLooper();
        Class<?> activity = Class.forName("android.app.ActivityThread");
        Context context = (Context) activity.getMethod("getSystemContext")
                .invoke(activity.getMethod("systemMain").invoke(null));
        CountDownLatch scanned = new CountDownLatch(args.length);
        java.util.concurrent.atomic.AtomicBoolean failed = new java.util.concurrent.atomic.AtomicBoolean();
        Thread worker = new Thread(() -> {
            try {
                MediaScannerConnection.scanFile(context, args, null, (path, uri) -> {
                    System.out.println("guest_media_scan=" + path + ":" + uri);
                    if (uri == null) failed.set(true);
                    scanned.countDown();
                });
                if (!scanned.await(30, TimeUnit.SECONDS)) throw new IllegalStateException("Guest media scan timeout");
                if (failed.get()) throw new IllegalStateException("Guest media provider returned no URI");
                System.out.println("guest_media_scan_complete=1");
                System.exit(0);
            } catch (Exception error) { error.printStackTrace(); System.exit(1); }
        }, "guest-media-scan");
        worker.start();
        Looper.loop();
    }
}
