package io.a10vm.guest;

import android.content.ContentProviderClient;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

public final class GuestFindDeviceCheck {
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Context context = (Context) thread.getMethod("getSystemContext")
                .invoke(thread.getMethod("systemMain").invoke(null));
        IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "activity");
        Object activity = Class.forName("android.app.IActivityManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        String authority = "com.xiaomi.finddevice.provider";
        IBinder token = new Binder();
        long start = SystemClock.elapsedRealtime();
        Object holder = LocalGuestService.call(activity, "android.app.IActivityManager", "getContentProviderExternal",
                authority, 0, token, "a10vm-finddevice-check");
        if (holder == null) throw new AssertionError("FindDevice provider missing");
        java.lang.reflect.Constructor<?> constructor = ContentProviderClient.class.getDeclaredConstructor(
                android.content.ContentResolver.class, Class.forName("android.content.IContentProvider"), boolean.class);
        constructor.setAccessible(true);
        try (ContentProviderClient client = (ContentProviderClient) constructor.newInstance(context.getContentResolver(),
                holder.getClass().getField("provider").get(holder), true)) {
            for (String name : new String[] {"isOpen", "isLastStatusOpen", "isLocked", "isLastStatusLocked", "lastSessionUserId"}) {
                try (Cursor cursor = client.query(Uri.parse("content://" + authority + "/" + name), null, null, null, null)) {
                    if (cursor == null || !cursor.moveToFirst() || cursor.getCount() != 1)
                        throw new AssertionError("FindDevice cursor missing: " + name);
                    System.out.println("finddevice_cursor_" + name + "=1");
                }
            }
        } finally {
            LocalGuestService.call(activity, "android.app.IActivityManager", "removeContentProviderExternal", authority, token);
        }
        long elapsed = SystemClock.elapsedRealtime() - start;
        if (elapsed > 5000) throw new AssertionError("FindDevice initialization took " + elapsed + " ms");
        System.out.println("finddevice_elapsed_ms=" + elapsed);
        System.out.println("finddevice_provider_ready=1");
    }
}
