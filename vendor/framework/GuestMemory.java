package io.a10vm.guest;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.system.Os;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

final class GuestMemory {
    private final GuestScheduling scheduling = new GuestScheduling();
    private int hostUid = -1;

    static void start() throws Exception {
        LocalSocket listener = new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
        listener.bind(new LocalSocketAddress("/dev/socket/lmkd", LocalSocketAddress.Namespace.FILESYSTEM));
        Os.chmod("/dev/socket/lmkd", 0600);
        LocalServerSocket server = new LocalServerSocket(listener.getFileDescriptor());
        GuestMemory memory = new GuestMemory();
        new Thread(() -> {
            try (LocalSocket held = listener; LocalServerSocket endpoint = server) {
                while (true) {
                    LocalSocket client = endpoint.accept();
                    new Thread(() -> memory.commands(client), "guest-memory-client").start();
                }
            } catch (Exception error) { System.err.println("Guest memory listener: " + error); }
        }, "guest-memory-listener").start();
        System.out.println("desktop_memory_ready=1");
    }

    // The host reclaims memory; publish guest app importance to its existing killer.
    private void commands(LocalSocket socket) {
        try (LocalSocket client = socket) {
            InputStream input = client.getInputStream();
            byte[] bytes = new byte[52];
            ByteBuffer packet = ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN);
            int size;
            while ((size = input.read(bytes)) > 0) {
                if (size < 4 || size % 4 != 0) throw new IllegalArgumentException("Guest memory packet size");
                packet.clear();
                packet.limit(size);
                int command = packet.getInt();
                synchronized (scheduling) {
                    switch (command) {
                        case 0:
                            if (size < 12 || (size - 4) % 8 != 0) throw new IllegalArgumentException("Guest memory targets");
                            break;
                        case 1:
                            if (size != 16) throw new IllegalArgumentException("Guest process priority");
                            int pid = packet.getInt(), uid = packet.getInt(), adjustment = packet.getInt();
                            if (pid <= 0 || uid < 0 || adjustment < -1000 || adjustment > 1000)
                                throw new IllegalArgumentException("Guest process identity");
                            if (uid >= 10000) {
                                kernelPriority(pid, adjustment);
                                scheduling.update(pid, adjustment);
                            }
                            break;
                        case 2:
                            if (size != 8) throw new IllegalArgumentException("Guest process removal");
                            int removed = packet.getInt();
                            scheduling.remove(removed);
                            break;
                        case 3:
                            if (size != 4) throw new IllegalArgumentException("Guest process reset");
                            scheduling.reset();
                            break;
                        case 4:
                            if (size != 12) throw new IllegalArgumentException("Guest memory kill count");
                            client.getOutputStream().write(ByteBuffer.allocate(8).order(ByteOrder.BIG_ENDIAN)
                                    .putInt(command).putInt(0).array());
                            break;
                        default: throw new IllegalArgumentException("Guest memory command " + command);
                    }
                }
            }
        } catch (Exception error) { System.err.println("Guest memory protocol: " + error); }
    }

    private void kernelPriority(int pid, int adjustment) {
        try (FileOutputStream output = new FileOutputStream("/proc/" + pid + "/oom_score_adj")) {
            if (hostUid < 0) hostUid = Os.stat("/proc/self").st_uid;
            if (Os.fstat(output.getFD()).st_uid != hostUid) return;
            // An ordinary UID can lower protection, but cannot grant negative OOM scores.
            output.write(Integer.toString(Math.max(0, adjustment)).getBytes(StandardCharsets.US_ASCII));
        } catch (Exception error) { System.err.println("Guest memory priority pid=" + pid + ": " + error); }
    }
}
