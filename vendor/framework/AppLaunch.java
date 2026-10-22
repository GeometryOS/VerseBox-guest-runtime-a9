package io.a10vm.guest;

import java.io.File;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

final class AppLaunch {
    static Process start(List<String> arguments, int sequence) throws Exception {
        String uid = null, gid = null, sdk = "29", name = "guest-application", data = null;
        int entry = -1;
        for (int i = 0; i < arguments.size(); ++i) {
            String argument = arguments.get(i);
            if (argument.startsWith("--setuid=")) uid = argument.substring(9);
            else if (argument.startsWith("--setgid=")) gid = argument.substring(9);
            else if (argument.startsWith("--target-sdk-version=")) sdk = argument.substring(21);
            else if (argument.startsWith("--nice-name=")) name = argument.substring(12);
            else if (argument.startsWith("--app-data-dir=")) data = argument.substring(15);
            else if (argument.equals("--invoke-with")) {
                throw new IllegalArgumentException("Guest launch wrapper is not configured");
            } else if (!argument.startsWith("--")) { entry = i; break; }
        }
        if (entry < 0 || uid == null || gid == null || Integer.parseInt(uid) < 0 || Integer.parseInt(gid) < 0)
            throw new IllegalArgumentException("Invalid guest application launch");
        List<String> command = new ArrayList<>();
        command.add(System.getenv("A10VM_EXEC_LINKER"));
        command.add(System.getenv("A10VM_APP_PROCESS"));
        command.add("-Xnoimage-dex2oat");
        command.addAll(AppRuntimeOptions.forProcess(name));
        command.add("-Djava.library.path=" + System.getenv("A10VM_LIBRARY_PATH"));
        command.add("/system/bin");
        command.add("--application");
        command.add("--nice-name=" + name);
        command.add(System.getenv().getOrDefault("A10VM_GUEST_APPLICATION_CLASS",
                "io.a10vm.guest.GuestApplication"));
        command.add(sdk);
        String main = arguments.get(entry);
        if (arguments.subList(0, entry).contains("--start-child-zygote")) {
            if (!main.equals("com.android.internal.os.WebViewZygoteInit") &&
                    !main.equals("com.android.internal.os.AppZygoteInit"))
                throw new IllegalArgumentException("Unknown guest child zygote " + main);
            command.add("io.a10vm.guest.GuestChildZygote");
            command.add(main);
        } else command.add(main);
        command.addAll(arguments.subList(entry + 1, arguments.size()));
        File state = new File(System.getenv("A10VM_LIBRARY_PATH")).getParentFile();
        ProcessBuilder builder = new ProcessBuilder(command).directory(new File(System.getenv("A10VM_ROOTFS"))).redirectErrorStream(true)
                .redirectOutput(logFile(sequence));
        Map<String, String> environment = builder.environment();
        // Preloading framework libraries exposes their bundled codecs to application JNI.
        // Keep only the container shims; app_process loads Android libraries via DT_NEEDED.
        List<String> preloads = new ArrayList<>();
        for (String path : environment.getOrDefault("LD_PRELOAD", "").split(":")) {
            for (String shim : new String[] {"property-client", "guest-devices", "binder-devices",
                    "guest-log", "malloc-leak-shim", "libdl-compat"}) {
                if (path.contains(shim)) { preloads.add(path); break; }
            }
        }
        environment.put("LD_PRELOAD", String.join(":", preloads));
        environment.put("A10VM_GUEST_UID", uid);
        environment.put("A10VM_GUEST_GID", gid);
        environment.remove("A10VM_DEX_CACHE");
        if (!"0".equals(System.getenv("A10VM_DEX_CACHE_ENABLED"))) {
            try {
                String cache = AppDexCache.directory(data, name, Integer.parseInt(uid));
                if (cache != null) environment.put("A10VM_DEX_CACHE", cache);
            } catch (ReflectiveOperationException error) {
                System.out.println("desktop_application_dex_cache_unavailable=" + name + ":" +
                        error.getClass().getSimpleName());
            }
        }
        environment.put("A10VM_APPLICATION_PARENT_PID", Integer.toString(android.os.Process.myPid()));
        environment.put("CLASSPATH", new File(state, "framework-check.dex").getPath());
        String mallocOptions = environment.remove("A10VM_DEBUG_APP_MALLOC");
        if (mallocOptions != null && "com.android.launcher3".equals(name))
            environment.put("LIBC_DEBUG_MALLOC_OPTIONS", mallocOptions);
        long started = android.os.SystemClock.elapsedRealtimeNanos();
        environment.put("A10VM_APPLICATION_START_NS", Long.toString(started));
        Process child = builder.start();
        System.out.print("desktop_application_launch=" + pid(child) + ":" + name + "\n");
        System.out.println("desktop_application_spawn_ms=" +
                (android.os.SystemClock.elapsedRealtimeNanos() - started) / 1000000 + " name=" + name);
        return child;
    }

    static File logFile(int sequence) {
        File state = new File(System.getenv("A10VM_LIBRARY_PATH")).getParentFile();
        return new File(state, "application-" + android.os.Process.myPid() + "-" + sequence + ".log");
    }

    static int pid(Process child) throws Exception {
        Field field = child.getClass().getDeclaredField("pid");
        field.setAccessible(true);
        return ((Number) field.get(child)).intValue();
    }
}
