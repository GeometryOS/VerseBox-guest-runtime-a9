package io.a10vm.guest;

import android.net.LocalSocketAddress;
import android.os.Looper;
import android.system.Os;
import android.system.OsConstants;

public final class GuestChildZygote {
    public static void main(String[] args) throws Exception {
        String socket = null, abi = null;
        int firstUid = -1, lastUid = -1;
        for (String arg : args) {
            if (arg.startsWith("--zygote-socket=")) socket = arg.substring(16);
            else if (arg.startsWith("--abi-list=")) abi = arg.substring(11);
            else if (arg.startsWith("--uid-range-start=")) firstUid = Integer.parseInt(arg.substring(18));
            else if (arg.startsWith("--uid-range-end=")) lastUid = Integer.parseInt(arg.substring(16));
        }
        if (socket == null || !"arm64-v8a".equals(abi) || firstUid < 90000 || lastUid < firstUid)
            throw new IllegalArgumentException("Invalid guest child zygote arguments");
        Os.prctl(OsConstants.PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        Looper.prepareMainLooper();
        DesktopProcesses.start(new LocalSocketAddress(socket, LocalSocketAddress.Namespace.ABSTRACT),
                firstUid, lastUid);
        System.out.println("guest_child_zygote_ready=" + socket);
        Looper.loop();
    }
}
