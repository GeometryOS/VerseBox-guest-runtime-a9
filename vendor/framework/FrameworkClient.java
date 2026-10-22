package io.a10vm.guest;

import android.os.Build;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Process;
import android.os.SystemClock;
import java.lang.reflect.Method;

public final class FrameworkClient {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected startup nonce");
        if (Build.VERSION.SDK_INT != 29) throw new AssertionError("Wrong framework SDK: " + Build.VERSION.SDK_INT);
        String nonce = args[0];
        Method get = Class.forName("android.os.ServiceManager").getMethod("getService", String.class);
        IBinder remote = null;
        long deadline = SystemClock.elapsedRealtime() + 5000;
        while (remote == null && SystemClock.elapsedRealtime() < deadline) {
            remote = (IBinder) get.invoke(null, "a10vm.framework." + nonce);
            if (remote == null) SystemClock.sleep(10);
        }
        if (remote == null || !remote.isBinderAlive()) throw new AssertionError("Missing framework service");
        int serverPid = 0;
        for (int i = 0; i < 64; ++i) {
            Parcel input = Parcel.obtain(), output = Parcel.obtain();
            try {
                input.writeInterfaceToken("io.a10vm.guest.framework");
                input.writeString(nonce);
                input.writeInt(i);
                if (!remote.transact(IBinder.FIRST_CALL_TRANSACTION, input, output, 0))
                    throw new AssertionError("Framework transaction failed");
                output.readException();
                if (!nonce.equals(output.readString()) || output.readInt() != i * 7)
                    throw new AssertionError("Wrong framework reply");
                int observed = output.readInt();
                if (observed == Process.myPid() || (serverPid != 0 && observed != serverPid) ||
                        output.readInt() != Process.myPid() || output.readInt() != Process.myUid())
                    throw new AssertionError("Wrong Binder identity");
                serverPid = observed;
            } finally { output.recycle(); input.recycle(); }
        }
        System.out.println("framework_sdk=" + Build.VERSION.SDK_INT);
        System.out.println("framework_nonce=" + nonce);
        System.out.println("framework_server_pid=" + serverPid);
        System.out.println("framework_client_pid=" + Process.myPid());
        ProcessEvidence.report("framework");
        System.out.println("framework_transactions=64\nframework_complete=1");
    }
}
