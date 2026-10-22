package io.a10vm.guest;

import java.io.FileInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

final class ProcessEvidence {
    private static final ThreadLocal<byte[]> PROC_BUFFER = ThreadLocal.withInitial(() -> new byte[4096]);

    static String read(String path) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (FileInputStream input = new FileInputStream(path)) {
            byte[] buffer = new byte[8192];
            int size;
            while ((size = input.read(buffer)) != -1) bytes.write(buffer, 0, size);
        }
        return new String(bytes.toByteArray(), StandardCharsets.UTF_8);
    }

    /** Read the numeric fields needed by scheduling without split/regex allocations. */
    static void readStat(String path, long[] fields) throws Exception {
        byte[] buffer = PROC_BUFFER.get();
        int length = 0;
        try (FileInputStream input = new FileInputStream(path)) {
            int size;
            while ((size = input.read(buffer, length, buffer.length - length)) > 0) {
                length += size;
                if (length == buffer.length) throw new IllegalArgumentException("proc stat too large");
            }
        }
        int close = -1;
        for (int index = length - 1; index >= 0; index--) {
            if (buffer[index] == ')') {
                close = index;
                break;
            }
        }
        if (close < 0) throw new IllegalArgumentException("proc stat command name");
        int field = 3;
        long priority = Long.MIN_VALUE;
        long started = Long.MIN_VALUE;
        int index = close + 1;
        while (index < length) {
            while (index < length && buffer[index] <= ' ') index++;
            if (index >= length) break;
            int end = index;
            while (end < length && buffer[end] > ' ') end++;
            if (field == 19 || field == 22) {
                long value = 0;
                boolean negative = buffer[index] == '-';
                int digit = negative ? index + 1 : index;
                for (; digit < end; digit++) {
                    int number = buffer[digit] - '0';
                    if (number < 0 || number > 9) throw new IllegalArgumentException("proc stat number");
                    value = value * 10 + number;
                }
                if (negative) value = -value;
                if (field == 19) priority = value;
                else {
                    started = value;
                    break;
                }
            }
            field++;
            index = end;
        }
        if (priority == Long.MIN_VALUE || started == Long.MIN_VALUE)
            throw new IllegalArgumentException("proc stat fields");
        fields[0] = started;
        fields[1] = priority;
    }

    static void report(String prefix) throws Exception {
        StringBuilder identity = new StringBuilder();
        for (String line : read("/proc/thread-self/status").split("\n")) {
            if (line.startsWith("Tgid:")) identity.append(prefix).append("_pid=").append(line.substring(5).trim()).append('\n');
            if (line.startsWith("Uid:")) {
                String[] ids = line.substring(4).trim().split("\\s+");
                identity.append(prefix).append("_uid=").append(ids[0]).append('\n');
                identity.append(prefix).append("_euid=").append(ids[1]).append('\n');
            }
            if (line.startsWith("CapEff:")) identity.append(prefix).append("_cap_eff=").append(line.substring(7).trim()).append('\n');
        }
        identity.append(prefix).append("_selinux=").append(read("/proc/self/attr/current").trim()).append('\n');
        System.out.print(identity.toString());
        for (String line : read("/proc/self/maps").split("\n")) {
            String[] fields = line.split("\\s+", 6);
            if (fields.length == 6 && fields[1].contains("x") && fields[5].startsWith("/"))
                System.out.print(prefix.toUpperCase(java.util.Locale.ROOT) + "_MAP\t" + fields[5] + "\n");
        }
    }
}
