package io.a10vm.guest;

import android.os.SystemClock;

public final class BootImageCheck {
    public static void main(String[] args) throws Exception {
        long started = Long.parseLong(System.getenv("A10VM_BOOT_IMAGE_START_NS"));
        System.out.println("boot_image_entry_ms=" + (SystemClock.elapsedRealtimeNanos() - started) / 1000000);
        Class.forName("android.graphics.Typeface");
        System.out.println("boot_image_ready_ms=" + (SystemClock.elapsedRealtimeNanos() - started) / 1000000);
        String maps = ProcessEvidence.read("/proc/self/maps");
        boolean boot = false, framework = false, executable = false;
        for (String line : maps.split("\n")) {
            String[] fields = line.split("\\s+", 6);
            if (fields.length != 6) continue;
            if (fields[5].endsWith("/boot.art")) boot = true;
            if (fields[5].endsWith("/boot-framework.art")) framework = true;
            if (fields[1].contains("x") && fields[5].contains("boot-image-boot-framework.oat"))
                executable = true;
            if (fields[5].endsWith(".art") || fields[5].contains("boot-image-"))
                System.out.println("BOOT_IMAGE_MAP\t" + line);
        }
        if ("loaded".equals(args[0]) && !(boot && framework && executable))
            throw new AssertionError("Boot image not mapped: boot=" + boot + " framework=" + framework + " executable=" + executable);
        if ("absent".equals(args[0]) && (boot || framework || executable))
            throw new AssertionError("Imageless control unexpectedly mapped a boot image");
        ProcessEvidence.report("boot_image");
        System.out.println("boot_image_complete=" + args[0]);
    }
}
