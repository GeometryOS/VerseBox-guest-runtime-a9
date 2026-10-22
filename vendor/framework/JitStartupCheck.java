package io.a10vm.guest;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class JitStartupCheck {
    private static volatile long sink;

    private static int mix(int value) {
        for (int index = 0; index < 32; ++index)
            value = (value * 1664525 + 1013904223) ^ (value >>> 7);
        return value;
    }

    private static long workload() {
        long result = 0;
        Map<String, Integer> counts = new HashMap<>();
        List<String> rows = new ArrayList<>();
        for (int index = 0; index < 12000; ++index) {
            String row = "item/" + (index % 100) + "/" + Integer.toHexString(mix(index));
            rows.add(row);
            String key = row.substring(0, row.lastIndexOf('/'));
            Integer old = counts.get(key);
            counts.put(key, old == null ? 1 : old + 1);
            result += row.hashCode();
        }
        for (String row : rows) result ^= mix(row.hashCode());
        for (Integer count : counts.values()) result += count;
        return result;
    }

    public static void main(String[] args) throws Exception {
        long expected = 0;
        for (int pass = 0; pass < 4; ++pass) {
            long started = System.nanoTime();
            long result = workload();
            double elapsed = (System.nanoTime() - started) / 1000000.0;
            if (pass == 0) expected = result;
            if (result != expected) throw new AssertionError("Compiled workload result changed");
            sink = result;
            System.out.println("jit_startup_pass=" + pass + " ms=" + elapsed + " result=" + result);
        }
        ProcessEvidence.report("jit_startup");
    }
}
