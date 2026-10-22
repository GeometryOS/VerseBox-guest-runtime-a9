package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * API 28 endpoints for the audioserver-owned native services.
 *
 * <p>AudioService does not talk to audioserver over a binder interface object:
 * native AudioSystem.cpp spins on "AudioPolicyService not published" and
 * "AudioFlinger not published" until ServiceManager has these names.  The native
 * client only checks that the binder exists, so no Java interface class is
 * involved (API 28 has no android.media.IAudioPolicyService runtime class).
 *
 * <p>The ROM's audioserver cannot provide them here: /system/bin/audioserver and
 * the audio HAL service are 32-bit ELF and this runtime is 64-bit only.  Android
 * 10 instead ships a project-built 64-bit audio HAL bridge; the API 28 audio
 * bridge is a later stage item, so these endpoints only need to unblock boot.
 * AudioService failures after that are non-fatal (SystemServer wraps them in
 * reportWtf).
 */
final class A9AudioPolicy {
    private static final String[] SERVICES = {
            "media.audio_policy",
            "media.audio_flinger",
            "media.sound_trigger_hw",
    };

    static void register() throws Exception {
        for (String name : SERVICES) {
            Binder binder = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code == INTERFACE_TRANSACTION || code == DUMP_TRANSACTION)
                        return super.onTransact(code, data, reply, flags);
                    // Zero of every return shape: void callers ignore the trailing
                    // bytes, int/boolean callers read a 0, and object callers read
                    // the null flag.  The audio bridge replaces these later.
                    if (reply != null && (flags & FLAG_ONEWAY) == 0) {
                        reply.writeNoException();
                        reply.writeInt(0);
                    }
                    return true;
                }
            };
            Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                    .invoke(null, name, binder);
        }
    }
}
