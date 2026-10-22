package io.a10vm.guest;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

public final class SystemToolsCheck {
    private static String run(String... command) throws Exception {
        Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
        try {
            if (!process.waitFor(15, TimeUnit.SECONDS))
                throw new AssertionError("System command timeout: " + Arrays.toString(command));
            byte[] bytes = new byte[32768];
            int size = process.getInputStream().read(bytes);
            String output = size < 0 ? "" : new String(bytes, 0, size, StandardCharsets.UTF_8);
            if (process.exitValue() != 0)
                throw new AssertionError(Arrays.toString(command) + ": " + output);
            return output;
        } finally {
            if (process.isAlive()) process.destroyForcibly();
        }
    }

    public static void main(String[] args) throws Exception {
        ProcessEvidence.report("system_tools");
        String root = System.getenv("A10VM_ROOTFS");
        org.json.JSONArray catalog = new org.json.JSONArray(ProcessEvidence.read(root + "/../system-image-paths.json"));
        for (int i = 0; i < catalog.length(); i++) {
            String path = catalog.getJSONObject(i).getString("path");
            android.system.Os.lstat(root + path);
        }
        System.out.println("system_image_paths_verified=" + catalog.length());
        String output = run("/system/bin/sh", "-c", "set -e; "
            + "test \"$(/system/bin/getprop ro.build.version.sdk)\" = 29; "
            + "test -x /system/bin/toybox; test -L /system/bin/ls; "
            + "/system/bin/toybox printf 'system_shell_native=1\\n'; "
            + "/system/bin/ls /system/bin | /system/bin/grep -x sqlite3; "
            + "test \"$(/system/bin/readlink /system/bin/ls)\" = toybox; "
            + "printf 'system_tools_paths=1\\n'");
        if (!output.contains("system_shell_native=1\n") || !output.contains("system_tools_paths=1\n"))
            throw new AssertionError(output);
        System.out.print(output);
        output = run("/bin/sh", "-c", "printf 'system_bin_alias=1\\n'");
        if (!output.contains("system_bin_alias=1\n")) throw new AssertionError(output);
        System.out.print(output);
        output = run("/system/bin/pm", "path", "com.android.settings");
        if (!output.contains("package:/system/product/priv-app/Settings/Settings.apk"))
            throw new AssertionError(output);
        System.out.print(output);
        System.out.println("system_package_script=1");
        System.out.println("system_tools_complete=1");
    }
}
