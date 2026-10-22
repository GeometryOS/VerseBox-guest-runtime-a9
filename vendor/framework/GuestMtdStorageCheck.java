package io.a10vm.guest;

import dalvik.system.PathClassLoader;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;

public final class GuestMtdStorageCheck {
    private static final int DIRECTORY = 1000000;
    private static final String NAME = "a10vm-storage-regression";

    public static void main(String[] args) throws Exception {
        ClassLoader loader = new PathClassLoader(
                "/system/framework/vendor.xiaomi.hardware.mtdservice-V1.0-java.jar:" +
                "/system/framework/vendor.xiaomi.hardware.mtdservice-V1.1-java.jar",
                GuestMtdStorageCheck.class.getClassLoader());
        Class<?> api = Class.forName("vendor.xiaomi.hardware.mtdservice.V1_1.IMTService", true, loader);
        Object service = api.getMethod("getService", String.class, boolean.class).invoke(null, "default", false);
        if (service == null) throw new AssertionError("MIUI persistent storage HAL missing");
        System.out.println("mtd_service_found=1");
        Class<?> callback = Class.forName(api.getName() + "$persist_readCallback", true, loader);
        Method read = api.getMethod("persist_read", int.class, String.class, callback);
        Method write = api.getMethod("persist_write", int.class, String.class, ArrayList.class, int.class);
        Method remove = api.getMethod("persist_remove", int.class, String.class);
        ArrayList<Byte> bytes = new ArrayList<>(Arrays.asList((byte) 0, (byte) 127, (byte) -128, (byte) -1));
        String mode = args.length == 0 ? "roundtrip" : args[0];
        if (mode.equals("roundtrip") || mode.equals("seed")) {
            assertCode(remove.invoke(service, DIRECTORY, NAME), 0);
            assertRead(service, read, callback, DIRECTORY, NAME, 41, new ArrayList<Byte>());
            assertCode(write.invoke(service, DIRECTORY, NAME, bytes, bytes.size()), 0);
            assertRead(service, read, callback, DIRECTORY, NAME, 0, bytes);
            System.out.println("mtd_binary_roundtrip=1");
            assertCode(write.invoke(service, DIRECTORY, NAME, bytes, 1), 22);
            assertRead(service, read, callback, DIRECTORY, NAME, 0, bytes);
            assertCode(write.invoke(service, DIRECTORY, "../invalid", bytes, bytes.size()), 22);
            System.out.println("mtd_failed_write_preserved_data=1");
        }
        if (mode.equals("read")) {
            assertRead(service, read, callback, DIRECTORY, NAME, 0, bytes);
            System.out.println("mtd_restart_persisted=1");
        }
        if (!mode.equals("seed")) {
            assertCode(remove.invoke(service, DIRECTORY, NAME), 0);
            assertCode(remove.invoke(service, DIRECTORY, NAME), 0);
            assertRead(service, read, callback, DIRECTORY, NAME, 41, new ArrayList<Byte>());
            System.out.println("mtd_remove_idempotent=1");
        }
        if (!"".equals(api.getMethod("getFid").invoke(service)))
            throw new AssertionError("Unexpected hardware credential");
        System.out.println("mtd_hardware_credential_absent=1");
    }

    private static void assertRead(Object service, Method method, Class<?> callback,
            int directory, String name, int code, ArrayList<Byte> bytes) throws Exception {
        int[] calls = {0};
        Object receiver = Proxy.newProxyInstance(callback.getClassLoader(), new Class<?>[] {callback},
                (proxy, invoked, args) -> {
                    if (invoked.getName().equals("onValues")) {
                        assertCode(args[0], code);
                        if (!bytes.equals(args[1])) throw new AssertionError("MTD data differs");
                        calls[0]++;
                    }
                    return null;
                });
        method.invoke(service, directory, name, receiver);
        if (calls[0] != 1) throw new AssertionError("MTD read callback count: " + calls[0]);
    }

    private static void assertCode(Object actual, int expected) {
        if (!Integer.valueOf(expected).equals(actual)) throw new AssertionError("MTD result " + actual + ", expected " + expected);
    }
}
