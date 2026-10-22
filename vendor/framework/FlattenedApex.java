package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Parcelable;
import java.io.File;
import java.lang.reflect.Array;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.json.JSONObject;

final class FlattenedApex extends Binder {
    private static final String DESCRIPTOR = "android.apex.IApexService";
    private final Map<Integer, String> transactions = new HashMap<>();
    private final Parcelable[] packages;

    private FlattenedApex() throws Exception {
        attachInterface(null, DESCRIPTOR);
        Class<?> stub = Class.forName(DESCRIPTOR + "$Stub");
        for (String name : new String[] {"getSessions", "getActivePackages", "getAllPackages", "getActivePackage"}) {
            java.lang.reflect.Field field = stub.getDeclaredField("TRANSACTION_" + name);
            field.setAccessible(true);
            transactions.put(field.getInt(null), name);
        }
        Class<?> info = Class.forName("android.apex.ApexInfo");
        List<Parcelable> modules = new ArrayList<>();
        File[] directories = new File("/system/apex").listFiles();
        if (directories == null) throw new IllegalStateException("Missing flattened APEX directories");
        for (File directory : directories) {
            File manifest = new File(directory, "apex_manifest.json");
            if (!directory.isDirectory() || !manifest.isFile()) continue;
            JSONObject metadata = new JSONObject(new String(Files.readAllBytes(manifest.toPath()), StandardCharsets.UTF_8));
            Object module = info.getConstructor().newInstance();
            info.getField("packageName").set(module, metadata.getString("name"));
            info.getField("packagePath").set(module, directory.getPath());
            info.getField("versionCode").setLong(module, metadata.getLong("version"));
            info.getField("versionName").set(module, metadata.optString("versionName", ""));
            info.getField("isFactory").setBoolean(module, true);
            info.getField("isActive").setBoolean(module, true);
            modules.add((Parcelable) module);
        }
        if (modules.isEmpty()) throw new IllegalStateException("Missing flattened APEX manifests");
        packages = modules.toArray((Parcelable[]) Array.newInstance(info, modules.size()));
    }

    static void register() throws Exception {
        FlattenedApex service = new FlattenedApex();
        Object proxy = Class.forName(DESCRIPTOR + "$Stub").getMethod("asInterface", IBinder.class).invoke(null, service);
        Object result = Class.forName(DESCRIPTOR).getMethod("getAllPackages").invoke(proxy);
        if (Array.getLength(result) != service.packages.length) throw new AssertionError("APEX parcel round trip failed");
        Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                .invoke(null, "apexservice", service);
        System.out.println("desktop_flattened_apex=" + service.packages.length);
    }

    @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
        if (code == INTERFACE_TRANSACTION) {
            reply.writeString(DESCRIPTOR);
            return true;
        }
        String operation = transactions.get(code);
        if (operation == null) throw new UnsupportedOperationException("Flattened APEX supports module queries");
        data.enforceInterface(DESCRIPTOR);
        if ("getSessions".equals(operation)) {
            reply.writeNoException();
            reply.writeInt(0);
        } else if ("getActivePackage".equals(operation)) {
            String requested = data.readString();
            Parcelable found = null;
            try {
                for (Parcelable module : packages)
                    if (requested.equals(module.getClass().getField("packageName").get(module))) found = module;
            } catch (ReflectiveOperationException error) { throw new IllegalStateException(error); }
            reply.writeNoException();
            reply.writeInt(found == null ? 0 : 1);
            if (found != null) found.writeToParcel(reply, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        } else {
            reply.writeNoException();
            reply.writeTypedArray(packages, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
        }
        return true;
    }
}
