package io.a10vm.guest;

import android.os.Process;
import java.util.concurrent.CountDownLatch;

public final class SchedulingCheck {
    public static void main(String[] args) throws Exception {
        CountDownLatch ready = new CountDownLatch(1), done = new CountDownLatch(1);
        int[] tid = {0};
        Thread render = new Thread(() -> {
            try {
                tid[0] = Process.myTid();
                Process.setThreadPriority(tid[0], -4);
                ready.countDown();
                done.await();
            } catch (Exception error) { throw new RuntimeException(error); }
        }, "schedule) render");
        render.start();
        if (!ready.await(3, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("Render thread priority");
        GuestScheduling scheduling = new GuestScheduling();
        try {
            checkStat(Process.myTid());
            checkStat(tid[0]);
            scheduling.update(Process.myPid(), 900);
            require(10, Process.myTid());
            require(10, tid[0]);
            Process.setThreadPriority(tid[0], -2);
            scheduling.update(Process.myPid(), 900);
            require(-2, tid[0]);
            scheduling.update(Process.myPid(), 200);
            require(5, tid[0]);
            scheduling.update(Process.myPid(), 0);
            require(-4, tid[0]);
            scheduling.reset();
            System.out.println("scheduling_stat_matches=1\nscheduling_thread_override_preserved=1\nscheduling_cached_nice=10\nscheduling_service_nice=5\nscheduling_render_restored=-4\nscheduling_ready=1");
        } finally { done.countDown(); render.join(); }
    }

    private static void checkStat(int tid) throws Exception {
        String path = "/proc/self/task/" + tid + "/stat";
        String raw = ProcessEvidence.read(path);
        String[] expected = raw.substring(raw.lastIndexOf(')') + 2).trim().split("\\s+");
        long[] fields = new long[2];
        ProcessEvidence.readStat(path, fields);
        if (fields[0] != Long.parseLong(expected[19]) || fields[1] != Process.getThreadPriority(tid))
            throw new AssertionError("proc stat identity/priority mismatch tid=" + tid);
    }

    private static void require(int expected, int tid) throws Exception {
        int actual = Process.getThreadPriority(tid);
        if (actual != expected) throw new AssertionError("tid=" + tid + " nice=" + actual + " expected=" + expected);
    }
}
