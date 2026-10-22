package io.a10vm.guest;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Message;
import android.os.Messenger;

final class GuestWifiChannel {
    private static Messenger endpoint;

    private static int code(String name) throws Exception {
        return Class.forName("com.android.internal.util.AsyncChannel").getField(name).getInt(null);
    }

    static synchronized Messenger get() throws Exception {
        if (endpoint != null) return endpoint;
        final int connect = code("CMD_CHANNEL_FULL_CONNECTION");
        final int connected = code("CMD_CHANNEL_FULLY_CONNECTED");
        final int disconnect = code("CMD_CHANNEL_DISCONNECT");
        final int disconnected = code("CMD_CHANNEL_DISCONNECTED");
        HandlerThread thread = new HandlerThread("guest-wifi-channel");
        thread.start();
        endpoint = new Messenger(new Handler(thread.getLooper()) {
            @Override public void handleMessage(Message request) {
                if (request.replyTo == null || (request.what != connect && request.what != disconnect)) return;
                Message reply = Message.obtain();
                reply.what = request.what == connect ? connected : disconnected;
                reply.replyTo = endpoint;
                try { request.replyTo.send(reply); }
                catch (android.os.RemoteException closed) { }
            }
        });
        return endpoint;
    }
}
