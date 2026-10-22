package io.a10vm.guest;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

public final class JitCheck {
    private static volatile int result;
    private static final class Value { int field = 17; }

    private static int readField(Value value) { return value.field; }

    private static int calculate(int seed) {
        for (int i = 0; i < 1000; ++i) seed = (seed * 1664525 + 1013904223) ^ (seed >>> 7);
        return seed;
    }

    public static void main(String[] args) throws Exception {
        for (int i = 0; i < 3000; ++i) result = calculate(i);
        java.lang.reflect.Method field = JitCheck.class.getDeclaredMethod("readField", Value.class);
        Value value = new Value();
        for (int i = 0; i < 3000; ++i) result = (Integer) field.invoke(null, value);
        Thread.sleep(200);
        long started = System.nanoTime();
        for (int i = 0; i < 3000; ++i) result = calculate(i);
        System.out.println("jit_workload_ms=" + (System.nanoTime() - started) / 1000000.0);
        String maps = new String(Files.readAllBytes(Paths.get("/proc/self/maps")), StandardCharsets.UTF_8);
        boolean compiler = false, cache = false;
        for (String line : maps.split("\n")) {
            String[] fields = line.split("\\s+", 6);
            if (fields.length < 6 || !fields[1].contains("x")) continue;
            if (line.contains("libart-compiler.so")) compiler = true;
            if (line.contains("jit-cache") || line.contains("jit-code-cache")) cache = true;
        }
        ProcessEvidence.report("jit");
        System.out.println("jit_compiler_mapped=" + compiler);
        System.out.println("jit_executable_cache=" + cache);
        try {
            field.invoke(null, new Object[] {null});
            throw new AssertionError("Expected compiled null pointer exception");
        } catch (java.lang.reflect.InvocationTargetException error) {
            if (!(error.getCause() instanceof NullPointerException)) throw error;
        }
        System.out.println("jit_implicit_null_exception=1");
        if (args.length > 0 && "--require-jit".equals(args[0]) && (!compiler || !cache))
            throw new AssertionError("ART JIT compiler and executable cache are required");
    }
}
