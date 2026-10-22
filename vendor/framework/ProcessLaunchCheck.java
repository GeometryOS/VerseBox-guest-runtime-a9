package io.a10vm.guest;

import java.io.File;
import java.io.DataInputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

public final class ProcessLaunchCheck {
    public static void main(String[] args) throws Exception {
        if (args.length != 0) {
            if (Arrays.equals(args, new String[] {"--isolated-code-paths"})) {
                if (!android.os.Process.isIsolated())
                    throw new AssertionError("Guest renderer identity is not isolated");
                java.lang.reflect.Field manager = Class.forName("android.app.ActivityThread")
                        .getDeclaredField("sPackageManager");
                manager.setAccessible(true);
                if (manager.get(null) != null)
                    throw new AssertionError("Isolated package manager was initialized early");
                GuestApplication.configureRuntime(29);
                if (manager.get(null) != null)
                    throw new AssertionError("Isolated process queried the package manager");
                System.out.println("isolated_code_paths_complete=1");
                return;
            }
            if (!Arrays.equals(args, new String[] {"--probe", "preserved-argument"}))
                throw new AssertionError("Generic main arguments changed");
            String cmdline = ProcessEvidence.read("/proc/self/cmdline");
            String name = cmdline.substring(0, cmdline.indexOf(0));
            if (!name.equals("webview-main-check"))
                throw new AssertionError("Guest kernel cmdline name: " + name);
            System.out.println("application_cmdline_complete=1");
            if (!"AndroidKeyStore".equals(java.security.KeyStore.getInstance("AndroidKeyStore").getType()))
                throw new AssertionError("AndroidKeyStore provider missing");
            System.out.println("application_keystore_provider=1");
            Class<?> loader = Class.forName("android.webkit.WebViewLibraryLoader");
            java.lang.reflect.Field reserved = loader.getDeclaredField("sAddressSpaceReserved");
            reserved.setAccessible(true);
            if (!reserved.getBoolean(null)) throw new AssertionError("WebView address space missing");
            System.out.println("generic_main_complete=1");
            return;
        }
        Process child = AppLaunch.start(Arrays.asList("--runtime-args", "--setuid=1037",
                "--setgid=1037", "--target-sdk-version=29", "--nice-name=webview-main-check",
                ProcessLaunchCheck.class.getName(), "--probe", "preserved-argument"), 1);
        try {
            if (!child.waitFor(15, TimeUnit.SECONDS) || child.exitValue() != 0)
                throw new AssertionError("Generic guest main did not finish");
            String log = ProcessEvidence.read(AppLaunch.logFile(1).getPath());
            if (!log.contains("generic_main_complete=1\n")) throw new AssertionError(log);
        } finally { if (child.isAlive()) child.destroyForcibly(); }
        child = AppLaunch.start(Arrays.asList("--runtime-args", "--setuid=99008",
                "--setgid=99008", "--target-sdk-version=29", "--nice-name=isolated-code-paths-check",
                ProcessLaunchCheck.class.getName(), "--isolated-code-paths"), 3);
        try {
            if (!child.waitFor(5, TimeUnit.SECONDS) || child.exitValue() != 0)
                throw new AssertionError("Isolated guest main failed: " +
                        ProcessEvidence.read(AppLaunch.logFile(3).getPath()));
            String log = ProcessEvidence.read(AppLaunch.logFile(3).getPath());
            if (!log.contains("isolated_code_paths_complete=1\n")) throw new AssertionError(log);
            System.out.println("isolated_code_paths_launch_complete=1");
        } finally { if (child.isAlive()) child.destroyForcibly(); }
        String socket = "a10vm.webview-check-" + android.os.Process.myPid();
        child = AppLaunch.start(Arrays.asList("--runtime-args", "--setuid=1053", "--setgid=1053",
                "--target-sdk-version=29", "--nice-name=webview-zygote-check", "--start-child-zygote",
                "com.android.internal.os.WebViewZygoteInit", "--zygote-socket=" + socket,
                "--abi-list=arm64-v8a", "--uid-range-start=99000", "--uid-range-end=99999"), 2);
        try {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
            while (!AppLaunch.logFile(2).isFile() || !ProcessEvidence.read(AppLaunch.logFile(2).getPath())
                    .contains("guest_child_zygote_ready=" + socket + "\n")) {
                if (!child.isAlive() || System.nanoTime() >= deadline)
                    throw new AssertionError("Guest child zygote startup failed");
                Thread.sleep(20);
            }
            for (int query = 0; query < 2; query++) {
                try (LocalSocket connection = new LocalSocket()) {
                    connection.connect(new LocalSocketAddress(socket, LocalSocketAddress.Namespace.ABSTRACT));
                    connection.setSoTimeout(3000);
                    OutputStream output = connection.getOutputStream();
                    output.write("1\n--query-abi-list\n".getBytes(StandardCharsets.US_ASCII));
                    output.flush();
                    DataInputStream input = new DataInputStream(connection.getInputStream());
                    int length = input.readInt();
                    if (length != 9) throw new AssertionError("Guest child ABI reply size");
                    byte[] abi = new byte[length];
                    input.readFully(abi);
                    if (!"arm64-v8a".equals(new String(abi, StandardCharsets.US_ASCII)))
                        throw new AssertionError("Guest child ABI reply");
                }
            }
            if (!child.isAlive()) throw new AssertionError("Guest child zygote returned from main");
            System.out.println("child_zygote_listener_complete=1");
            System.out.println("process_launch_complete=1");
        } finally {
            child.destroyForcibly();
            child.waitFor(3, TimeUnit.SECONDS);
        }
    }
}
