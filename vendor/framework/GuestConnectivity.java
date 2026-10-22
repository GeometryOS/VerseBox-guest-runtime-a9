package io.a10vm.guest;

import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.ProxyInfo;
import android.os.FileObserver;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

public final class GuestConnectivity {
    private static final List<GuestConnectivity> agents = new ArrayList<>();
    private static JSONObject current = new JSONObject();
    private static Handler handler;
    private static FileObserver observer;
    private static volatile GuestConnectivity active;
    private static String applied = "";
    private final Messenger endpoint;
    private final List<Message> pending = new ArrayList<>();
    private Messenger peer;
    private final int netId;
    private final long hostNetId;
    private final Handler validationHandler;
    private NetworkInfo info;
    private static int protocol(String name) throws Exception {
        return Class.forName(name.startsWith("EVENT_")
                ? "android.net.NetworkAgent" : "com.android.internal.util.AsyncChannel").getField(name).getInt(null);
    }

    static void start() {
        if (!"1".equals(System.getenv("A10VM_HOST_NETWORK"))) return;
        HandlerThread thread = new HandlerThread("guest-host-network");
        thread.start();
        handler = new Handler(thread.getLooper());
        observer = new FileObserver(System.getenv("A10VM_STATE_ROOT"), FileObserver.MOVED_TO | FileObserver.CLOSE_WRITE) {
            @Override public void onEvent(int event, String name) {
                if ("host-network.json".equals(name)) handler.post(GuestConnectivity::refresh);
            }
        };
        observer.startWatching();
        handler.post(GuestConnectivity::refresh);
    }

    static synchronized JSONObject snapshot() { return current; }

    private static void refresh() {
        try {
            IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                    .getMethod("checkService", String.class).invoke(null, "connectivity");
            if (binder == null) { handler.postDelayed(GuestConnectivity::refresh, 250); return; }
            Path path = Paths.get(System.getenv("A10VM_STATE_ROOT"), "host-network.json");
            if (!Files.isRegularFile(path)) return;
            String text = new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
            if (text.equals(applied)) return;
            JSONObject snapshot = new JSONObject(text);
            synchronized (GuestConnectivity.class) { current = snapshot; }
            if (active != null && (!snapshot.optBoolean("connected") ||
                    snapshot.optLong("hostNetId") != active.hostNetId)) {
                active.info = networkInfo(new JSONObject().put("connected", false));
                active.send("EVENT_NETWORK_INFO_CHANGED", active.info);
                active = null;
            }
            if (snapshot.optBoolean("connected")) {
                if (active == null) {
                    active = new GuestConnectivity(binder, snapshot);
                    agents.add(active);
                    Files.write(Paths.get(System.getenv("A10VM_STATE_ROOT"), "guest-network-id"),
                            (active.netId + "\n").getBytes(StandardCharsets.UTF_8));
                    System.out.println("guest_host_network_registered=" + active.netId);
                } else {
                    active.info = networkInfo(snapshot);
                    active.send("EVENT_NETWORK_PROPERTIES_CHANGED", links(snapshot));
                    active.send("EVENT_NETWORK_CAPABILITIES_CHANGED", capabilities(snapshot));
                    active.send("EVENT_NETWORK_INFO_CHANGED", active.info);
                    active.publishValidation();
                }
            }
            GuestWifi.publish(snapshot);
            applied = text;
        } catch (Exception error) {
            System.err.println("Guest host network: " + error);
            handler.postDelayed(GuestConnectivity::refresh, 1000);
        }
    }

    private GuestConnectivity(IBinder binder, JSONObject snapshot) throws Exception {
        info = networkInfo(snapshot);
        hostNetId = snapshot.getLong("hostNetId");
        endpoint = new Messenger(new Handler(handler.getLooper()) {
            @Override public void handleMessage(Message message) {
                try {
                    if (message.what == protocol("CMD_CHANNEL_FULL_CONNECTION")) {
                        peer = message.replyTo;
                        Message reply = Message.obtain(null, protocol("CMD_CHANNEL_FULLY_CONNECTED"), 0, 0);
                        reply.replyTo = endpoint;
                        peer.send(reply);
                        for (Message item : pending) peer.send(item);
                        pending.clear();
                        publishValidation();
                    } else if (message.what == protocol("CMD_CHANNEL_DISCONNECT")) {
                        if (peer != null) peer.send(Message.obtain(null, protocol("CMD_CHANNEL_DISCONNECTED")));
                        peer = null;
                        agents.remove(GuestConnectivity.this);
                    } else if (message.what == Class.forName("android.net.NetworkAgent")
                            .getField("CMD_START_SOCKET_KEEPALIVE").getInt(null)) {
                        Message reply = Message.obtain(null, protocol("EVENT_SOCKET_KEEPALIVE"), message.arg1, -30);
                        if (peer != null) peer.send(reply);
                    }
                } catch (Exception error) { System.err.println("Guest network channel: " + error); }
            }
        });
        Class<?> misc = Class.forName("android.net.NetworkMisc");
        Object service = Class.forName("android.net.IConnectivityManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        java.lang.reflect.Field tracker = service.getClass().getDeclaredField("mTrackerHandler");
        tracker.setAccessible(true);
        validationHandler = (Handler) tracker.get(service);
        netId = (Integer) LocalGuestService.call(service, "android.net.IConnectivityManager", "registerNetworkAgent",
                endpoint, info, links(snapshot), capabilities(snapshot), 60, misc.getConstructor().newInstance(), -1);
    }

    public static int validationResult(int proposed, int netId) {
        GuestConnectivity relay = active;
        JSONObject host = snapshot();
        return relay != null && relay.netId == netId && host.optBoolean("connected")
                && relay.hostNetId == host.optLong("hostNetId") ? (host.optBoolean("validated") ? 1 : 0) : proposed;
    }

    private void publishValidation() throws Exception {
        int event = Class.forName("com.android.server.ConnectivityService").getField("EVENT_NETWORK_TESTED").getInt(null);
        validationHandler.obtainMessage(event, validationResult(0, netId), netId, "").sendToTarget();
    }

    private void send(String event, Object value) throws Exception {
        Message message = Message.obtain(null, protocol(event), value);
        if (peer == null) pending.add(message); else peer.send(message);
    }

    private static NetworkInfo networkInfo(JSONObject snapshot) throws Exception {
        JSONArray transports = snapshot.optJSONArray("transports");
        boolean wifi = contains(transports, NetworkCapabilities.TRANSPORT_WIFI);
        boolean mobile = contains(transports, NetworkCapabilities.TRANSPORT_CELLULAR);
        NetworkInfo info = NetworkInfo.class.getConstructor(int.class, int.class, String.class, String.class)
                .newInstance(wifi ? ConnectivityManager.TYPE_WIFI : mobile ? ConnectivityManager.TYPE_MOBILE :
                        ConnectivityManager.TYPE_ETHERNET, 0, wifi ? "WIFI" : mobile ? "MOBILE" : "ETHERNET", "");
        NetworkInfo.class.getMethod("setIsAvailable", boolean.class).invoke(info, snapshot.optBoolean("connected"));
        NetworkInfo.class.getMethod("setDetailedState", NetworkInfo.DetailedState.class, String.class, String.class)
                .invoke(info, snapshot.optBoolean("connected") ? NetworkInfo.DetailedState.CONNECTED :
                        NetworkInfo.DetailedState.DISCONNECTED, "host", wifi && snapshot.optBoolean("connected")
                                ? GuestWifi.ssid(snapshot.optJSONObject("wifi")) : null);
        return info;
    }

    private static boolean contains(JSONArray values, int value) {
        if (values != null) for (int i = 0; i < values.length(); ++i) if (values.optInt(i) == value) return true;
        return false;
    }

    static NetworkCapabilities capabilities(JSONObject snapshot) throws Exception {
        NetworkCapabilities caps = new NetworkCapabilities();
        JSONArray transports = snapshot.getJSONArray("transports");
        boolean relayed = false;
        // The guest owns a regular relay, not the host VPN's TUN interface or UID ranges.
        for (int i = 0; i < transports.length(); ++i) {
            int type = transports.getInt(i);
            if (type == NetworkCapabilities.TRANSPORT_VPN) continue;
            NetworkCapabilities.class.getMethod("addTransportType", int.class).invoke(caps, type);
            relayed = true;
        }
        if (!relayed) NetworkCapabilities.class.getMethod("addTransportType", int.class)
                .invoke(caps, NetworkCapabilities.TRANSPORT_ETHERNET);
        if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            JSONObject wifi = snapshot.optJSONObject("wifi");
            NetworkCapabilities.class.getMethod("setSSID", String.class).invoke(caps, GuestWifi.ssid(wifi));
            if (wifi != null && wifi.has("rssi")) NetworkCapabilities.class.getMethod("setSignalStrength", int.class)
                    .invoke(caps, wifi.getInt("rssi"));
        }
        for (String key : new String[] {"internet", "validated", "notMetered", "notRoaming", "notSuspended"}) {
            int value = key.equals("internet") ? NetworkCapabilities.NET_CAPABILITY_INTERNET :
                    key.equals("validated") ? NetworkCapabilities.NET_CAPABILITY_VALIDATED :
                    key.equals("notMetered") ? NetworkCapabilities.NET_CAPABILITY_NOT_METERED :
                    key.equals("notRoaming") ? NetworkCapabilities.NET_CAPABILITY_NOT_ROAMING :
                    NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED;
            NetworkCapabilities.class.getMethod(snapshot.optBoolean(key) ? "addCapability" : "removeCapability",
                    int.class).invoke(caps, value);
        }
        NetworkCapabilities.class.getMethod("setLinkDownstreamBandwidthKbps", int.class)
                .invoke(caps, snapshot.optInt("downstreamKbps"));
        NetworkCapabilities.class.getMethod("setLinkUpstreamBandwidthKbps", int.class)
                .invoke(caps, snapshot.optInt("upstreamKbps"));
        return caps;
    }

    private static LinkProperties links(JSONObject snapshot) throws Exception {
        LinkProperties links = new LinkProperties();
        links.setInterfaceName(snapshot.optString("interface"));
        if (snapshot.optInt("mtu") != 0) links.setMtu(snapshot.getInt("mtu"));
        links.setDomains(snapshot.optString("domains", null));
        JSONArray addresses = snapshot.getJSONArray("addresses"), dns = snapshot.getJSONArray("dns");
        for (int i = 0; i < addresses.length(); ++i)
            LinkProperties.class.getMethod("addLinkAddress", LinkAddress.class).invoke(links,
                    LinkAddress.class.getConstructor(String.class).newInstance(addresses.getString(i)));
        for (int i = 0; i < dns.length(); ++i)
            LinkProperties.class.getMethod("addDnsServer", InetAddress.class)
                    .invoke(links, InetAddress.getByName(dns.getString(i)));
        JSONArray routes = snapshot.getJSONArray("routes");
        Class<?> prefix = Class.forName("android.net.IpPrefix"), route = Class.forName("android.net.RouteInfo");
        for (int i = 0; i < routes.length(); ++i) {
            JSONObject value = routes.getJSONObject(i);
            Object entry = route.getConstructor(prefix, InetAddress.class, String.class, int.class).newInstance(
                    prefix.getConstructor(String.class).newInstance(value.getString("destination")),
                    value.optString("gateway").isEmpty() ? null : InetAddress.getByName(value.getString("gateway")),
                    value.optString("interface"), value.getInt("type"));
            LinkProperties.class.getMethod("addRoute", route).invoke(links, entry);
        }
        JSONObject proxy = snapshot.optJSONObject("proxy");
        if (proxy != null && !proxy.optString("host").isEmpty())
            links.setHttpProxy(ProxyInfo.buildDirectProxy(proxy.getString("host"), proxy.getInt("port")));
        return links;
    }
}
