package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import java.lang.reflect.Method;

/**
 * API 28 media service endpoints that the 32-bit ROM binaries cannot provide.
 *
 * <p>The Redroid 9 image ships {@code /system/bin/mediaserver} and its media
 * HAL as ELF32, while this runtime is AArch64-only, so nothing ever publishes
 * the framework's native media services.  Applications do not fail fast when
 * they are absent: {@code ServiceManager.getService()} waits its full 5 s
 * timeout for each name.  The measured symptom (2026-10-23, instance
 * 1792721001107, SystemUI) is five {@code Waiting for service
 * media.extractor...} records plus {@code Skipped 35/41 frames} while opening
 * an app - the 10-20 s freeze the user reports.
 *
 * <p>These are guest-side no-op endpoints, the same boundary as
 * {@link A9AudioPolicy}: they make the lookup succeed immediately so the app's
 * media path falls through to its normal failure handling instead of blocking
 * the UI thread.  They do not decode or play anything; the real bridge is the
 * later audio/media stage.  A name that the ROM already published is left
 * untouched.
 */
final class A9MediaPolicy {
    private static final String[] SERVICES = {
            "media.extractor",
            "media.player",
            "media.codec",
            "media.omx",
            "media.cas",
            "media.metrics",
            "media.resource_manager",
            "drm.drmManager",
    };

    static void register() throws Exception {
        Class<?> manager = Class.forName("android.os.ServiceManager");
        Method getService = manager.getMethod("getService", String.class);
        Method addService = manager.getMethod("addService", String.class, IBinder.class);
        int published = 0;
        for (String name : SERVICES) {
            if (getService.invoke(null, name) != null) continue;
            Binder binder = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code == INTERFACE_TRANSACTION || code == DUMP_TRANSACTION)
                        return super.onTransact(code, data, reply, flags);
                    // Zero of every return shape: a binder-returning call reads
                    // the null flag, an int/boolean call reads 0, and a void
                    // call ignores the trailing bytes.
                    if (reply != null && (flags & FLAG_ONEWAY) == 0) {
                        reply.writeNoException();
                        reply.writeInt(0);
                    }
                    return true;
                }
            };
            addService.invoke(null, name, binder);
            ++published;
        }
        System.out.println("a9_media_policy=registered:" + published);
    }
}
