package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import dalvik.system.PathClassLoader;
import java.io.File;
import java.lang.reflect.Method;

final class GuestCredentialCapabilities {
    static final String SDK = "/system/framework/security-device-credential-sdk.jar";

    static ClassLoader loader() {
        return new PathClassLoader(SDK, GuestCredentialCapabilities.class.getClassLoader());
    }

    static void register() throws Exception {
        if (!new File(SDK).isFile()) return;
        ClassLoader loader = loader();
        Class<?> api = Class.forName("com.xiaomi.security.devicecredential.SecurityDeviceCredentialManager", true, loader);
        String name = (String) api.getField("SERVICE_NAME").get(null);
        Class<?> manager = Class.forName("android.os.ServiceManager");
        Method check = manager.getMethod("checkService", String.class);
        if (check.invoke(null, name) != null) return;
        String descriptor = (String) api.getField("DESCRIPTOR").get(null);
        int supported = api.getField("TRANSACTION_isThisDeviceSupported").getInt(null);
        int deviceId = api.getField("TRANSACTION_getSecurityDeviceId").getInt(null);
        int sign = api.getField("TRANSACTION_sign").getInt(null);
        int reload = api.getField("TRANSACTION_forceReload").getInt(null);
        Class<?> listener = Class.forName("com.xiaomi.security.devicecredential.IOnRemoteCallFinishedListener", true, loader);
        String callbackDescriptor = (String) listener.getField("DESCRIPTOR").get(null);
        int idCallback = listener.getField("TRANSACTION_ON_GET_SECURITY_DEIVCE_ID_FINISHED").getInt(null);
        int signCallback = listener.getField("TRANSACTION_ON_SIGN_FINISHED").getInt(null);
        int reloadCallback = listener.getField("TRANSACTION_ON_FORCE_RELOAD").getInt(null);
        int unsupported = Class.forName(api.getName() + "$OperationFailedException", true, loader)
                .getField("ERROR_CODE_NOT_SUPPORTED").getInt(null);
        Binder endpoint = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws RemoteException {
                if (code != supported && code != deviceId && code != sign && code != reload)
                    return super.onTransact(code, data, reply, flags);
                data.enforceInterface(descriptor);
                if (code == supported) {
                    reply.writeNoException();
                    reply.writeInt(0);
                    return true;
                }
                IBinder callback = data.readStrongBinder();
                if (callback == null) throw new IllegalArgumentException("Missing credential callback");
                if (code == sign) {
                    data.readInt();
                    data.createByteArray();
                    data.readInt();
                }
                Parcel result = Parcel.obtain();
                try {
                    result.writeInterfaceToken(callbackDescriptor);
                    result.writeInt(unsupported);
                    if (code == deviceId) result.writeString(null);
                    else if (code == sign) result.writeByteArray(null);
                    callback.transact(code == deviceId ? idCallback : code == sign ? signCallback : reloadCallback,
                            result, null, IBinder.FLAG_ONEWAY);
                    reply.writeNoException();
                } finally { result.recycle(); }
                return true;
            }
        };
        endpoint.attachInterface(null, descriptor);
        manager.getMethod("addService", String.class, IBinder.class).invoke(null, name, endpoint);
        System.out.println("guest_credential_capability=" + name + " supported=false");
    }
}
