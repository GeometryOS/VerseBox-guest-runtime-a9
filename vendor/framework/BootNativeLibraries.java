package io.a10vm.guest;

public final class BootNativeLibraries {
    private BootNativeLibraries() {}

    public static void load() {
        String libraries = System.getenv("A10VM_BOOT_JNI");
        if (libraries == null || libraries.isEmpty()) return;
        for (String library : libraries.split(":")) {
            System.loadLibrary(library);
            System.out.println("rom_boot_jni=" + library);
        }
    }
}
