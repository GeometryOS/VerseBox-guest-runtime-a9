package io.a10vm.guest;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import io.a10vm.framework.PackageQueryReplies;
import java.io.FileDescriptor;
import java.lang.reflect.Field;

/** Delegates Java package queries while preserving the framework's concrete client. */
final class PackageQueryClient {
    private PackageQueryClient() {}

    static synchronized void install() throws Exception {
        if (android.os.Process.isIsolated()) return;
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Object manager = thread.getDeclaredMethod("getPackageManager").invoke(null);
        Class<?> api = Class.forName("android.content.pm.IPackageManager");
        IBinder remote = (IBinder) api.getMethod("asBinder").invoke(manager);
        if (remote instanceof QueryBinder) return;
        Object client = Class.forName("android.content.pm.IPackageManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, new QueryBinder(remote));
        Field field = thread.getDeclaredField("sPackageManager");
        field.setAccessible(true);
        field.set(null, client);
    }

    static final class QueryBinder extends Binder {
        private final IBinder remote;
        QueryBinder(IBinder remote) throws RemoteException {
            this.remote = remote;
            attachInterface(null, remote.getInterfaceDescriptor());
        }
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            boolean handled = remote.transact(code, data, reply, flags);
            if (reply != null && (flags & IBinder.FLAG_ONEWAY) == 0) {
                PackageQueryReplies.adaptForCaller(code, reply, handled, android.os.Process.myUid());
                reply.setDataPosition(0);
            }
            return handled;
        }
        @Override public boolean pingBinder() { return remote.pingBinder(); }
        @Override public boolean isBinderAlive() { return remote.isBinderAlive(); }
        @Override public void dump(FileDescriptor fd, String[] args) {
            try { remote.dump(fd, args); } catch (RemoteException error) { PackageQueryClient.<RuntimeException>raise(error); }
        }
        @Override public void dumpAsync(FileDescriptor fd, String[] args) {
            try { remote.dumpAsync(fd, args); } catch (RemoteException error) { PackageQueryClient.<RuntimeException>raise(error); }
        }
        @Override public void linkToDeath(DeathRecipient recipient, int flags) {
            try { remote.linkToDeath(recipient, flags); }
            catch (RemoteException error) { PackageQueryClient.<RuntimeException>raise(error); }
        }
        @Override public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
            return remote.unlinkToDeath(recipient, flags);
        }
    }

    @SuppressWarnings("unchecked")
    private static <T extends Throwable> void raise(Throwable error) throws T { throw (T) error; }
}
