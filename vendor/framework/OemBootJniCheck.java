package io.a10vm.guest;

public final class OemBootJniCheck {
    public static void main(String[] args) throws Exception {
        Class<?> performance = Class.forName("com.qualcomm.qti.Performance");
        Object instance = performance.getConstructor().newInstance();
        Object value = performance.getMethod("perfGetProp", String.class, String.class)
                .invoke(instance, "a10vm.test.unset_property", "a10vm-default");
        if (!"a10vm-default".equals(value)) throw new AssertionError("OEM property JNI default: " + value);
        System.out.println("oem_boot_jni_ready=1");
    }
}
