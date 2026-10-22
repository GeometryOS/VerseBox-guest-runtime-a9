package io.a10vm.guest;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public final class PropertyCheck {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("Expected role and nonce");
        Class<?> properties = Class.forName("android.os.SystemProperties");
        Method get = properties.getMethod("get", String.class);
        Method set = properties.getMethod("set", String.class, String.class);
        if (!"29".equals(get.invoke(null, "ro.build.version.sdk"))) throw new AssertionError("Wrong guest SDK");
        if (!"missing".equals(args[0])) verifyBuildProperties(get);
        if ("missing".equals(args[0])) {
            try { set.invoke(null, "sys.a10vm.check", args[1]); }
            catch (InvocationTargetException failure) {
                if (!(failure.getCause() instanceof RuntimeException)) throw failure;
                System.out.println("property_missing_rejected=1");
                return;
            }
            throw new AssertionError("Missing property service accepted a write");
        }
        if ("writer".equals(args[0])) {
            set.invoke(null, "sys.a10vm.check", args[1]);
            set.invoke(null, "sys.a10vm.empty", "");
            boolean rejected = false;
            try { set.invoke(null, "ro.build.version.sdk", "30"); }
            catch (InvocationTargetException failure) { rejected = failure.getCause() instanceof RuntimeException; }
            if (!rejected || !"29".equals(get.invoke(null, "ro.build.version.sdk")))
                throw new AssertionError("Read-only property changed");
            System.out.println("property_readonly_rejected=1");
        }
        if (!args[1].equals(get.invoke(null, "sys.a10vm.check")) || !"".equals(get.invoke(null, "sys.a10vm.empty")))
            throw new AssertionError("Private property round trip failed");
        ProcessEvidence.report("property");
        System.out.println("property_" + args[0] + "_ready=1");
    }

    private static void verifyBuildProperties(Method get) throws Exception {
        for (String path : new String[] {"/default.prop", "/system/etc/prop.default", "/system/build.prop",
                "/vendor/default.prop", "/vendor/build.prop", "/odm/etc/build.prop", "/product/build.prop"}) {
            if (java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(path)).length == 0)
                throw new AssertionError("Empty guest property file: " + path);
        }
        if (!android.os.Build.MODEL.equals(get.invoke(null, "ro.product.model")) ||
                android.os.Build.MODEL.equals("unknown") || android.os.Build.FINGERPRINT.equals("unknown") ||
                !android.os.Build.FINGERPRINT.equals(get.invoke(null, "ro.build.fingerprint")) ||
                !android.os.Build.VERSION.SECURITY_PATCH.equals(get.invoke(null, "ro.build.version.security_patch")) ||
                android.os.Build.TIME <= 0 || !"a10vm".equals(get.invoke(null, "ro.hardware")) ||
                !"arm64-v8a".equals(get.invoke(null, "ro.product.cpu.abilist")) ||
                !"".equals(get.invoke(null, "ro.product.cpu.abilist32")))
            throw new AssertionError("Guest Build fields or runtime property overrides missing");
        System.out.println("property_build_files_complete=1");
        System.out.println("property_build_model=" + android.os.Build.MODEL);
        System.out.println("property_build_fingerprint=" + android.os.Build.FINGERPRINT);
    }
}
