package io.a10vm.guest;

import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class GuestInstall {
    public static void main(String[] args) throws Exception {
        if (args.length == 0) throw new IllegalArgumentException("Expected base APK and optional splits");
        long size = 0;
        for (String path : args) {
            File apk = new File(path);
            if (!apk.isFile() || apk.length() == 0) throw new IllegalArgumentException("Missing APK: " + path);
            size = Math.addExact(size, apk.length());
        }
        String created = GuestCommand.capture(new String[] {"package", "install-create", "-r", "-t",
                "--user", "0", "-S", Long.toString(size)}, null);
        Matcher match = Pattern.compile("\\[(\\d+)\\]").matcher(created);
        if (!match.find()) throw new IllegalStateException("Missing package session: " + created);
        String session = match.group(1);
        boolean committed = false;
        try {
            for (int i = 0; i < args.length; i++) {
                System.out.print(GuestCommand.capture(new String[] {"package", "install-write", "-S",
                        Long.toString(new File(args[i]).length()), session,
                        i == 0 ? "base.apk" : "split-" + i + ".apk", "-"}, args[i]));
            }
            System.out.print(GuestCommand.capture(new String[] {"package", "install-commit", session}, null));
            committed = true;
            System.out.println("guest_install_complete=1");
        } finally {
            if (!committed) {
                try { GuestCommand.capture(new String[] {"package", "install-abandon", session}, null); }
                catch (Exception cleanup) { System.err.println("Abandon install session: " + cleanup); }
            }
        }
    }
}
