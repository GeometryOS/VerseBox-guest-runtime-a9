package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;

public final class FrameworkServer {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected startup nonce");
        final String nonce = args[0];
        Binder service = new Binder() {
            @Override protected boolean onTransact(int code, Parcel input, Parcel output, int flags)
                    throws RemoteException {
                if (code != IBinder.FIRST_CALL_TRANSACTION) return super.onTransact(code, input, output, flags);
                input.enforceInterface("io.a10vm.guest.framework");
                if (!nonce.equals(input.readString())) throw new SecurityException("Framework nonce mismatch");
                output.writeNoException();
                output.writeString(nonce);
                output.writeInt(input.readInt() * 7);
                output.writeInt(android.os.Process.myPid());
                output.writeInt(Binder.getCallingPid());
                output.writeInt(Binder.getCallingUid());
                return true;
            }
        };
        Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                .invoke(null, "a10vm.framework." + nonce, service);
        ProcessEvidence.report("framework");
        System.out.println("framework_service_registered=" + nonce);
        Looper.prepare();
        Looper.loop();
    }
}
