package io.a10vm.guest;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class DesktopInput {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected private input socket");
        Class<?> type = Class.forName("android.hardware.input.InputManager");
        Object manager = type.getMethod("getInstance").invoke(null);
        Method inject = type.getMethod("injectInputEvent", InputEvent.class, int.class);
        ProcessEvidence.report("desktop_input");
        try (LocalSocket socket = new LocalSocket()) {
            socket.connect(new LocalSocketAddress(args[0], LocalSocketAddress.Namespace.FILESYSTEM));
            InputStream input = socket.getInputStream();
            OutputStream output = socket.getOutputStream();
            System.out.println("desktop_input_ready=1");
            byte[] bytes = new byte[192];
            int previous = 0;
            long downTime = 0;
            MotionEvent last = null;
            try {
                for (;;) {
                    int offset = 0;
                    while (offset < bytes.length) {
                        int read = input.read(bytes, offset, bytes.length - offset);
                        // A zero-length read means the peer closed; without this the loop spins forever.
                        if (read <= 0) throw new IllegalStateException("Truncated desktop input");
                        offset += read;
                    }
                    ByteBuffer packet = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
                    // The header is read before validation so that a malformed packet is skipped
                    // rather than tearing down the whole desktop session.
                    if (packet.getInt() != 0x54434832) { System.err.println("desktop_input_bad_magic"); continue; }
                    int running = packet.getInt(), sequence = packet.getInt(), action = packet.getInt();
                    int index = packet.getInt(), count = packet.getInt();
                    long time = packet.getLong() / 1000000;
                    if (running == 0) break;
                    if (running != 1 || count < 0 || count > 10 ||
                            index < 0 || (count > 0 && index >= count) || time < 0) {
                        System.err.println("desktop_input_bad_header=" + sequence);
                        continue;
                    }
                    MotionEvent event;
                    if (action == MotionEvent.ACTION_CANCEL && count == 0 && last != null) {
                        event = MotionEvent.obtain(last);
                        event.setAction(MotionEvent.ACTION_CANCEL);
                    } else {
                        if (count == 0) { System.err.println("desktop_input_bad_count=" + sequence); continue; }
                        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
                        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[count];
                        for (int i = 0; i < count; ++i) {
                            properties[i] = new MotionEvent.PointerProperties();
                            properties[i].id = packet.getInt();
                            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
                            coords[i] = new MotionEvent.PointerCoords();
                            coords[i].x = packet.getInt();
                            coords[i].y = packet.getInt();
                            coords[i].pressure = packet.getFloat();
                            coords[i].size = 1;
                        }
                        if (action == MotionEvent.ACTION_DOWN) downTime = time;
                        event = MotionEvent.obtain(downTime, time, action | (index << 8), count, properties,
                                coords, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
                    }
                    boolean accepted = false;
                    try {
                        try { accepted = (Boolean) inject.invoke(manager, event, 0); }
                        finally {
                            if (last != null) last.recycle();
                            last = MotionEvent.obtain(event);
                            event.recycle();
                        }
                    } catch (Exception failure) {
                        System.err.println("desktop_input_inject_fault=" + sequence + " " + failure);
                    }
                    output.write(ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN)
                            .putInt(0x4131494e).putInt(sequence).putInt(accepted ? 1 : 0).array());
                    output.flush();
                    if (!accepted) System.err.println("desktop_input_rejected=" + sequence);
                    previous = sequence;
                }
            } catch (Exception failure) {
                // Never let one malformed session kill the injector; the host reconnects.
                System.err.println("desktop_input_loop_exit=" + failure);
            } finally { if (last != null) last.recycle(); }
        }
    }
}
