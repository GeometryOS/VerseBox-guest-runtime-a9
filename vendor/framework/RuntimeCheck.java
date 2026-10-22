package io.a10vm.guest;

import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;

public final class RuntimeCheck {
    public static void main(String[] arguments) throws Exception {
        if (arguments.length != 1) throw new IllegalArgumentException("Expected startup nonce");
        System.out.println("art_version=" + System.getProperty("java.vm.version"));
        System.out.println("art_name=" + System.getProperty("java.vm.name"));
        AtomicInteger value = new AtomicInteger();
        Thread thread = new Thread(() -> value.set(42), "a10vm-runtime-check");
        thread.start();
        thread.join(3000);
        if (thread.isAlive() || value.get() != 42) throw new IllegalStateException("Guest Java thread failed");
        String path = "/data/local/tmp/runtime-check.txt";
        try (FileOutputStream output = new FileOutputStream(path)) {
            output.write(arguments[0].getBytes(StandardCharsets.UTF_8));
        }
        if (!ProcessEvidence.read(path).equals(arguments[0])) throw new IllegalStateException("Guest file round trip failed");
        DirectoryCheck.main(new String[]{"/data/local/tmp/runtime-directory-check/nested"});
        ProcessEvidence.report("art");
        System.out.println("art_nonce=" + arguments[0]);
        System.out.println("art_thread=1\nart_file_roundtrip=1\nart_directory_roundtrip=1\nart_runtime_ready=1");
    }
}
