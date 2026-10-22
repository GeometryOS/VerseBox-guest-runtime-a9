package io.a10vm.guest;

import android.app.DownloadManager;
import android.content.ContentUris;
import android.content.ContentProviderClient;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Looper;
import android.os.Binder;
import android.os.IBinder;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import org.json.JSONObject;

public final class GuestDownloadCheck {
    public static void main(String[] args) throws Exception {
        try { verify(args); }
        catch (Throwable failure) { failure.printStackTrace(); System.exit(1); }
    }

    private static void verify(String[] args) throws Exception {
        if (args.length != 3) throw new IllegalArgumentException("Expected URL, destination and SHA-256");
        Looper.prepareMainLooper();
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Context context = (Context) thread.getMethod("getSystemContext")
                .invoke(thread.getMethod("systemMain").invoke(null));
        File destination = new File(args[1]);
        if (!destination.getParentFile().isDirectory() && !destination.getParentFile().mkdirs())
            throw new IllegalStateException("Download directory missing");
        IBinder activityBinder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "activity");
        Object activity = Class.forName("android.app.IActivityManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, activityBinder);
        IBinder token = new Binder();
        Object holder = LocalGuestService.call(activity, "android.app.IActivityManager", "getContentProviderExternal",
                "downloads", 0, token, "a10vm-download-install");
        if (holder == null) throw new AssertionError("Download provider missing");
        Class<?> provider = Class.forName("android.content.IContentProvider");
        java.lang.reflect.Constructor<?> constructor = ContentProviderClient.class.getDeclaredConstructor(
                android.content.ContentResolver.class, provider, boolean.class);
        constructor.setAccessible(true);
        ContentProviderClient client = (ContentProviderClient) constructor.newInstance(context.getContentResolver(),
                holder.getClass().getField("provider").get(holder), true);
        DownloadManager.Request request = new DownloadManager.Request(Uri.parse(args[0]))
                .setDestinationUri(Uri.fromFile(destination))
                .setTitle("A10VM download installation check")
                .setMimeType("application/vnd.android.package-archive");
        java.lang.reflect.Method values = DownloadManager.Request.class.getDeclaredMethod("toContentValues", String.class);
        values.setAccessible(true);
        long id = ContentUris.parseId(client.insert(Uri.parse("content://downloads/my_downloads"),
                (ContentValues) values.invoke(request, "android")));
        JSONObject result = new JSONObject().put("id", id);
        long deadline = System.nanoTime() + 60_000_000_000L;
        String[] columns = {"status", "_data", "current_bytes", "total_bytes", "errorMsg"};
        try {
            while (true) {
                try (Cursor cursor = client.query(ContentUris.withAppendedId(
                        Uri.parse("content://downloads/all_downloads"), id), columns, null, null, null)) {
                    if (cursor == null || !cursor.moveToFirst()) throw new AssertionError("Download task missing");
                    int status = cursor.getInt(0);
                    result.put("status", status).put("path", cursor.isNull(1) ? JSONObject.NULL : cursor.getString(1))
                            .put("currentBytes", cursor.getLong(2)).put("totalBytes", cursor.getLong(3))
                            .put("error", cursor.isNull(4) ? JSONObject.NULL : cursor.getString(4));
                    if (status >= 200 && status != 200) throw new AssertionError("Download finalization failed: " + result);
                    if (status == 200) break;
                }
                if (System.nanoTime() >= deadline) throw new AssertionError("Download timed out: " + result);
                Thread.sleep(200);
            }
            File downloaded = new File(result.getString("path"));
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (FileInputStream stream = new FileInputStream(downloaded)) {
                byte[] buffer = new byte[65536];
                for (int count; (count = stream.read(buffer)) != -1;) digest.update(buffer, 0, count);
            }
            StringBuilder sha = new StringBuilder();
            for (byte value : digest.digest()) sha.append(String.format("%02x", value & 255));
            result.put("sha256", sha.toString()).put("fileBytes", downloaded.length());
            if (!sha.toString().equals(args[2]) || downloaded.length() != result.getLong("totalBytes"))
                throw new AssertionError("Downloaded APK content changed: " + result);
            GuestCommand.run(new String[] {"package", "install", "-r", "-t", "--user", "0", "-S",
                    Long.toString(downloaded.length())}, downloaded.getPath());
            result.put("installed", true);
            System.out.println("guest_download_install=" + result);
        } finally {
            client.delete(ContentUris.withAppendedId(Uri.parse("content://downloads/all_downloads"), id), null, null);
            client.close();
            LocalGuestService.call(activity, "android.app.IActivityManager", "removeContentProviderExternal",
                    "downloads", token);
        }
    }
}
