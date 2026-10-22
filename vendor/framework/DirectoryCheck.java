package io.a10vm.guest;

import java.io.File;
import java.nio.file.Files;

public final class DirectoryCheck {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected guest test directory");
        File directory = new File(args[0]);
        System.out.println("directory_root=" + System.getenv("A10VM_ROOTFS"));
        System.out.println("directory_canonical=" + directory.getCanonicalPath());
        if (!directory.mkdirs() && !directory.isDirectory())
            throw new IllegalStateException("Guest mkdirs failed: " + directory);
        if (!directory.getCanonicalPath().equals(directory.getPath()))
            throw new IllegalStateException("Guest canonical path exposed host storage");
        Files.delete(directory.toPath());
        Files.delete(directory.getParentFile().toPath());
        System.out.println("directory_check_passed=1");
    }
}
