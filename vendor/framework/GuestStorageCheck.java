package io.a10vm.guest;

import android.database.sqlite.SQLiteDatabase;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;

public final class GuestStorageCheck {
    public static void main(String[] args) throws Exception {
        int user = Class.forName("android.os.UserHandle").getField("USER_SYSTEM").getInt(null);
        PrivateVolumes.register();
        for (String method : new String[] {"getDataSystemDeDirectory", "getDataSystemCeDirectory"}) {
            File directory = (File) Class.forName("android.os.Environment").getMethod(method, int.class)
                    .invoke(null, user);
            if (!directory.isDirectory()) throw new AssertionError("User storage missing before services: " + directory);
            Path marker = directory.toPath().resolve("a10vm-bootstrap-check");
            Files.write(marker, new byte[] {42});
            File database = new File(directory, "a10vm-bootstrap-check.db");
            SQLiteDatabase sql = SQLiteDatabase.openOrCreateDatabase(database, null);
            try { sql.execSQL("CREATE TABLE bootstrap_check (value INTEGER)"); }
            finally { sql.close(); }
            PrivateVolumes.register();
            if (Files.readAllBytes(marker)[0] != 42 || !database.isFile())
                throw new AssertionError("User storage preparation replaced existing data");
        }
        System.out.println("guest_storage_ready_before_services=1");
        System.out.println("guest_storage_database_opened=1");
        System.out.println("guest_storage_existing_data_preserved=1");
    }
}
