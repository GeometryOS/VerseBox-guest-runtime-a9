package io.a10vm.guest;

import android.os.PersistableBundle;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.Set;

final class PrivateVolumes implements InvocationHandler {
    private Object listener;
    private final Set<Integer> unlocked = new HashSet<>();

    static void register() throws Exception {
        PrivateVolumes volumes = new PrivateVolumes();
        int systemUser = Class.forName("android.os.UserHandle").getField("USER_SYSTEM").getInt(null);
        volumes.prepare(systemUser);
        LocalGuestService.register("vold", "android.os.IVold", volumes);
        Set<Integer> active = new HashSet<>();
        LocalGuestService.register("storaged", "android.os.IStoraged", (proxy, method, args) -> {
            if ("onUserStarted".equals(method.getName())) { active.add((Integer) args[0]); return null; }
            if ("onUserStopped".equals(method.getName())) { active.remove((Integer) args[0]); return null; }
            if ("getRecentPerf".equals(method.getName())) return -1;
            throw new UnsupportedOperationException("Private storage statistics: " + method.getName());
        });
    }

    private void prepare(int user) throws Exception {
        if (user != 0) throw new UnsupportedOperationException("Guest user " + user);
        for (String directory : new String[] {"/data/user/0", "/data/user_de/0", "/data/system_ce/0",
                "/data/system_de/0", "/data/misc_ce/0", "/data/misc_de/0", "/data/media/0", "/storage/emulated/0",
                "/storage/emulated/0/Android/data", "/storage/emulated/0/Android/obb", "/storage/emulated/0/Android/media"})
            Files.createDirectories(Paths.get(directory));
        alias("/storage/self/primary", "../emulated/0");
        alias("/mnt/user/0/primary", "../../../storage/emulated/0");
        for (String mode : new String[] {"default", "read", "write", "full"})
            alias("/mnt/runtime/" + mode + "/emulated", "../../../storage/emulated");
    }

    private static void alias(String name, String target) throws Exception {
        Path path = Paths.get(name);
        if (Files.isSymbolicLink(path) && Files.readSymbolicLink(path).toString().equals(target)) return;
        if (Files.exists(path, LinkOption.NOFOLLOW_LINKS))
            throw new IllegalStateException("Guest storage alias already occupied: " + name);
        Files.createDirectories(path.getParent());
        Files.createSymbolicLink(path, Paths.get(target));
    }

    private void notify(String method, Object... args) throws Exception {
        if (listener == null) throw new IllegalStateException("Missing guest volume listener");
        LocalGuestService.call(listener, "android.os.IVoldListener", method, args);
    }

    @Override public synchronized Object invoke(Object proxy, Method method, Object[] args) throws Exception {
        String name = method.getName();
        switch (name) {
            case "setListener": listener = args[0]; return null;
            case "needsCheckpoint": case "needsRollback": case "supportsCheckpoint":
            case "supportsFileCheckpoint": case "supportsBlockCheckpoint": return false;
            case "commitChanges": case "monitor": return null;
            case "remountUid": return null;
            case "mkdirs":
                Path path = Paths.get((String) args[0]).normalize();
                if (!path.startsWith("/storage/emulated/0") && !path.startsWith("/storage/self/primary") &&
                        !path.startsWith("/mnt/runtime") && !path.startsWith("/mnt/user/0/primary"))
                    throw new IllegalArgumentException("Unknown guest external storage path: " + path);
                Files.createDirectories(path);
                return null;
            case "reset":
                prepare(0);
                notify("onVolumeCreated", "emulated", 2, null, null);
                notify("onVolumeInternalPathChanged", "emulated", "/data/media");
                notify("onVolumePathChanged", "emulated", "/storage/emulated");
                return null;
            case "mount":
                if (!"emulated".equals(args[0])) throw new IllegalArgumentException("Unknown private volume");
                prepare(0);
                notify("onVolumeStateChanged", "emulated", 2);
                return null;
            case "onUserAdded": case "onUserStarted":
                prepare((Integer) args[0]); return null;
            case "createUserKey":
                prepare((Integer) args[0]); return method.getReturnType() == boolean.class ? true : null;
            case "initUser0": prepare(0); return null;
            case "prepareUserStorage": prepare((Integer) args[1]); return null;
            case "unlockUserKey": prepare((Integer) args[0]); unlocked.add((Integer) args[0]); return null;
            case "lockUserKey": unlocked.remove((Integer) args[0]); return null;
            case "onUserStopped": case "onSecureKeyguardStateChanged": case "shutdown": return null;
            case "fstrim": case "runIdleMaint": case "abortIdleMaint":
                Object task = args[args.length - 1];
                if (task != null) LocalGuestService.call(task, "android.os.IVoldTaskListener", "onFinished", -95,
                        new PersistableBundle());
                return null;
            case "mountAppFuse": throw new UnsupportedOperationException("Guest file proxy descriptors");
            default:
                if ("1".equals(System.getenv("A10VM_TOLERANT_VOLD")))
                    return defaultValue(method.getReturnType());
                throw new UnsupportedOperationException("Private directory volume: " + name);
        }
    }

    /** Android 9 exposes vold operations that Android 10 removed; no-op them without a service. */
    private static Object defaultValue(Class<?> type) {
        if (type == boolean.class) return false;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        return null;
    }
}
