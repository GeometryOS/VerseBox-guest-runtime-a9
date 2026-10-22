package io.a10vm.guest;

import android.system.Os;
import android.os.Process;
import java.io.File;
import java.util.HashMap;
import java.util.Map;

/** CPU priority for guest apps sharing the host UID; no cgroup or core binding. */
final class GuestScheduling {
    private final Map<Integer, State> processes = new HashMap<>();
    private final long[] stat = new long[2];
    private int hostUid = -1;
    private static final class State {
        long started;
        int priority;
        final Map<Integer, long[]> threads = new HashMap<>();
    }

    void update(int pid, int adjustment) {
        try {
            String path = "/proc/" + pid;
            if (hostUid < 0) hostUid = Os.stat("/proc/self").st_uid;
            if (Os.stat(path).st_uid != hostUid) return;
            ProcessEvidence.readStat(path + "/stat", stat);
            long started = stat[0];
            int priority = adjustment >= 900 ? 10 : adjustment >= 200 ? 5 : 0;
            State state = processes.get(pid);
            if (state == null || state.started != started) {
                state = new State();
                state.started = started;
                state.priority = -1;
                processes.put(pid, state);
            }
            if (state.priority == priority) return;
            File[] tasks = new File(path + "/task").listFiles();
            if (tasks == null) return;
            for (File task : tasks) {
                try {
                    int tid = Integer.parseInt(task.getName());
                    ProcessEvidence.readStat(task.getPath() + "/stat", stat);
                    long threadStart = stat[0];
                    int current = (int) stat[1];
                    long[] original = state.threads.get(tid);
                    if (original == null || original[0] != threadStart) {
                        original = new long[] {threadStart, current};
                        state.threads.put(tid, original);
                    }
                    int target = priority == 0 ? (int) original[1] : Math.max(priority, (int) original[1]);
                    if (target != current) Process.setThreadPriority(tid, target);
                } catch (java.io.FileNotFoundException gone) { }
                catch (IllegalArgumentException gone) { }
            }
            state.priority = priority;
        } catch (Exception error) { System.err.println("Guest scheduling pid=" + pid + ": " + error); }
    }

    void remove(int pid) { processes.remove(pid); }

    void reset() {
        for (int pid : new java.util.ArrayList<>(processes.keySet())) update(pid, 0);
        processes.clear();
    }
}
