package io.a10vm.guest;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.system.Os;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

final class GuestDiscovery {
    static void start() throws Exception {
        LocalSocket listener = new LocalSocket();
        listener.bind(new LocalSocketAddress("/dev/socket/mdns", LocalSocketAddress.Namespace.FILESYSTEM));
        Os.chmod("/dev/socket/mdns", 0600);
        LocalServerSocket server = new LocalServerSocket(listener.getFileDescriptor());
        new Thread(() -> {
            try (LocalSocket held = listener; LocalServerSocket endpoint = server) {
                while (true) {
                    LocalSocket client = endpoint.accept();
                    new Thread(() -> commands(client), "guest-mdns-connection").start();
                }
            } catch (Exception error) { System.err.println("Guest discovery listener: " + error); }
        }, "guest-mdns-listener").start();
    }

    private static void commands(LocalSocket socket) {
        try (LocalSocket client = socket) {
            InputStream input = client.getInputStream();
            ByteArrayOutputStream command = new ByteArrayOutputStream();
            int next;
            while ((next = input.read()) >= 0) {
                if (next != 0) {
                    if (command.size() >= 65536) throw new IllegalArgumentException("Guest discovery command size");
                    command.write(next);
                    continue;
                }
                String request = new String(command.toByteArray(), StandardCharsets.UTF_8);
                command.reset();
                int space = request.indexOf(' ');
                if (space < 1) throw new IllegalArgumentException("Guest discovery command sequence");
                int sequence = Integer.parseInt(request.substring(0, space));
                String reply = "502 " + sequence + " Guest multicast discovery unsupported\0";
                client.getOutputStream().write(reply.getBytes(StandardCharsets.UTF_8));
                client.getOutputStream().flush();
            }
        } catch (Exception error) { System.err.println("Guest discovery protocol: " + error); }
    }
}
