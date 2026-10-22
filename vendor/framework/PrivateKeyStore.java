package io.a10vm.guest;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

final class PrivateKeyStore implements InvocationHandler {
    private final Map<Integer, Boolean> locked = new HashMap<>();

    static void register() throws Exception {
        if ("1".equals(System.getenv("A10VM_NATIVE_KEYSTORE"))) {
            Method check = Class.forName("android.os.ServiceManager").getMethod("checkService", String.class);
            long deadline = android.os.SystemClock.elapsedRealtime() + 5000;
            while (check.invoke(null, "android.security.keystore") == null) {
                if (android.os.SystemClock.elapsedRealtime() >= deadline)
                    throw new IllegalStateException("Native guest keystore did not register");
                android.os.SystemClock.sleep(10);
            }
            System.out.println("desktop_native_keystore_ready=1");
            return;
        }
        LocalGuestService.register("android.security.keystore", "android.security.keystore.IKeystoreService",
                new PrivateKeyStore());
    }

    @Override public synchronized Object invoke(Object proxy, Method method, Object[] args) {
        switch (method.getName()) {
            case "exist": return 7;
            case "list": return new String[0];
            case "getState": return 3;
            // This service stores no aliases, so clearing an application's keys is complete.
            case "clear_uid": return 1;
            case "onUserAdded": locked.put((Integer) args[0], true); return 1;
            case "onUserRemoved": locked.remove((Integer) args[0]); return 1;
            case "onKeyguardVisibilityChanged":
                locked.put((Integer) args[1], (Boolean) args[0]); return 1;
            default: throw new UnsupportedOperationException("Uninitialized guest key store: " + method.getName());
        }
    }
}
