package io.a10vm.guest;

import dalvik.system.PathClassLoader;
import java.lang.reflect.Method;

public final class GuestAccountCapabilityCheck {
    public static void main(String[] args) throws Exception {
        String apk = args.length == 0 ? "/system/app/XiaomiAccount/XiaomiAccount.apk" : args[0];
        ClassLoader loader = new PathClassLoader(apk + ":" + GuestCredentialCapabilities.SDK,
                GuestAccountCapabilityCheck.class.getClassLoader());
        Class<?> sdk = Class.forName("com.xiaomi.security.devicecredential.SecurityDeviceCredentialManager", true, loader);
        boolean hardware = (Boolean) sdk.getMethod("isThisDeviceSupported").invoke(null);
        Class<?> adapter = Class.forName("com.xiaomi.account.finddevice.b", true, loader);
        Method supported = adapter.getMethod("a");
        boolean advertised = (Boolean) supported.invoke(adapter.getConstructor().newInstance());
        System.out.println("account_hardware_credential_supported=" + hardware);
        System.out.println("account_finddevice_supported=" + advertised);
        if (hardware)
            throw new AssertionError("FindDevice replaced the guest endpoint with unsupported hardware claims");
        if (advertised)
            throw new AssertionError("Account requires FindDevice without a hardware credential backend");
        System.out.println("account_capability_ready=1");
    }
}
