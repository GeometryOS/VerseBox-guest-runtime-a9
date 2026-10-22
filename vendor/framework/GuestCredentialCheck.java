package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;

public final class GuestCredentialCheck {
    public static void main(String[] args) throws Exception {
        if ("fdpp".equals(args[0])) {
            IBinder endpoint = null;
            long deadline = SystemClock.elapsedRealtime() + 3000;
            while (endpoint == null && SystemClock.elapsedRealtime() < deadline) {
                endpoint = service("miui.fdpp");
                if (endpoint == null) Thread.sleep(10);
            }
            if (endpoint == null || !endpoint.pingBinder()) throw new AssertionError("ROM fdpp did not register");
            System.out.println("fdpp_endpoint=" + endpoint.getInterfaceDescriptor());
            System.out.println("fdpp_registered=1");
            return;
        }
        if ("server".equals(args[0])) {
            registerFixturePermission();
            GuestCredentialCapabilities.register();
            System.out.println("credential_server_ready=1");
            Thread.sleep(30000);
            return;
        }
        Class<?> api = Class.forName("com.xiaomi.security.devicecredential.SecurityDeviceCredentialManager",
                true, GuestCredentialCapabilities.loader());
        long start = SystemClock.elapsedRealtime();
        if ((Boolean) api.getMethod("isThisDeviceSupported").invoke(null))
            throw new AssertionError("Hardware credential reported without a backend");
        System.out.println("credential_supported_false=1");
        for (String method : new String[] {"getSecurityDeviceId", "signWithDeviceCredential", "forceReload"}) {
            try {
                if (method.equals("signWithDeviceCredential"))
                    api.getMethod(method, byte[].class).invoke(null, (Object) new byte[] {1, 2, 3});
                else api.getMethod(method).invoke(null);
                throw new AssertionError("Credential operation returned success: " + method);
            } catch (InvocationTargetException failure) {
                Throwable error = failure.getCause();
                if (!error.getClass().getName().equals(api.getName() + "$OperationFailedException") ||
                        error.getClass().getField("errorCode").getInt(error) != -100) throw failure;
            }
            System.out.println("credential_" + method + "_completed=1");
        }
        long elapsed = SystemClock.elapsedRealtime() - start;
        if (elapsed >= 2000) throw new AssertionError("Credential calls exceeded the account SDK timeout");
        System.out.println("credential_elapsed_ms=" + elapsed);
        IBinder before = service((String) api.getField("SERVICE_NAME").get(null));
        GuestCredentialCapabilities.register();
        if (service((String) api.getField("SERVICE_NAME").get(null)) != before)
            throw new AssertionError("Existing credential endpoint replaced");
        System.out.println("credential_existing_service_preserved=1");
    }

    private static IBinder service(String name) throws Exception {
        return (IBinder) Class.forName("android.os.ServiceManager").getMethod("checkService", String.class)
                .invoke(null, name);
    }

    private static void registerFixturePermission() throws Exception {
        Class<?> stub = Class.forName("android.os.IPermissionController$Stub");
        Field descriptorField = stub.getDeclaredField("DESCRIPTOR");
        descriptorField.setAccessible(true);
        String descriptor = (String) descriptorField.get(null);
        Field transactionField = stub.getDeclaredField("TRANSACTION_checkPermission");
        transactionField.setAccessible(true);
        int transaction = transactionField.getInt(null);
        Binder permission = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws RemoteException {
                if (code != transaction) return super.onTransact(code, data, reply, flags);
                data.enforceInterface(descriptor);
                String name = data.readString();
                data.readInt();
                int uid = data.readInt();
                reply.writeNoException();
                reply.writeInt(uid == 0 && "com.xiaomi.permission.fdpp".equals(name) ? 1 : 0);
                return true;
            }
        };
        permission.attachInterface(null, descriptor);
        Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                .invoke(null, "permission", permission);
    }
}
