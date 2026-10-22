package io.a10vm.guest;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.os.IBinder;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;

public final class NetworkCheck {
    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("--capabilities")) {
            for (int[] transports : new int[][] {{1, 4}, {4}}) {
                org.json.JSONObject host = new org.json.JSONObject().put("transports",
                        new org.json.JSONArray(transports)).put("internet", true).put("validated", true);
                NetworkCapabilities caps = GuestConnectivity.capabilities(host);
                if (!caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
                        || GuestConnectivity.capabilities(host.put("validated", false))
                            .hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED))
                    throw new AssertionError("Host network validation was not mirrored");
                if (caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN) ||
                        !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN))
                    throw new AssertionError("Host VPN leaked into guest network ownership");
                if (!caps.hasTransport(transports.length == 2 ? NetworkCapabilities.TRANSPORT_WIFI :
                        NetworkCapabilities.TRANSPORT_ETHERNET))
                    throw new AssertionError("Guest relayed transport missing");
            }
            org.json.JSONObject wifi = new org.json.JSONObject().put("ssid", "\"relay-test\"")
                    .put("networkId", 7).put("rssi", -55).put("frequency", 5200).put("linkSpeed", 150);
            android.net.wifi.WifiInfo info = GuestWifi.connectionInfo(new org.json.JSONObject()
                    .put("connected", true).put("transports", new org.json.JSONArray(new int[] {1})).put("wifi", wifi));
            if (!"\"relay-test\"".equals(info.getSSID()) || info.getRssi() != -55 || info.getFrequency() != 5200)
                throw new AssertionError("Host Wi-Fi snapshot fields changed");
            android.net.wifi.WifiInfo redacted = GuestWifi.connectionInfo(new org.json.JSONObject()
                    .put("connected", true).put("transports", new org.json.JSONArray(new int[] {1}))
                    .put("wifi", new org.json.JSONObject().put("networkId", -1)));
            if (redacted.getSupplicantState() != android.net.wifi.SupplicantState.COMPLETED)
                throw new AssertionError("Redacted host Wi-Fi ID was mistaken for a disconnection");
            verifyWifiIdentity();
            verifyWifiConsumers();
            System.out.println("network_capabilities_complete=1");
            return;
        }
        if (args.length > 0 && args[0].equals("--api")) {
            for (String type : new String[] {"android.net.INetd", "android.net.IDnsResolver"})
                for (java.lang.reflect.Method method : Class.forName(type).getDeclaredMethods()) System.out.println(method);
            return;
        }
        ProcessEvidence.report("network");
        Class<?> activity = Class.forName("android.app.ActivityThread");
        android.os.Looper.prepareMainLooper();
        Context context = (Context) activity.getMethod("getSystemContext")
                .invoke(activity.getMethod("systemMain").invoke(null));
        ConnectivityManager manager = context.getSystemService(ConnectivityManager.class);
        NetworkInfo info = manager.getActiveNetworkInfo();
        Network network = manager.getActiveNetwork();
        if (info == null || !info.isConnected() || network == null) throw new AssertionError("Guest default network missing");
        NetworkCapabilities caps = manager.getNetworkCapabilities(network);
        System.out.println("network_connected=" + info.getTypeName());
        System.out.println("network_validated=" + caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED));
        android.net.wifi.WifiManager wireless = context.getSystemService(android.net.wifi.WifiManager.class);
        boolean feature = context.getPackageManager().hasSystemFeature("android.hardware.wifi");
        System.out.println("network_wifi_feature=" + feature);
        System.out.println("network_wifi_state=" + wireless.getWifiState());
        System.out.println("network_wifi_supplicant=" + wireless.getConnectionInfo().getSupplicantState());
        org.json.JSONObject host = new org.json.JSONObject(ProcessEvidence.read(
                System.getenv("A10VM_STATE_ROOT") + "/host-network.json"));
        if (host.optBoolean("validated") != caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED))
            throw new AssertionError("Guest network validation differs from the host");
        if (!feature) throw new AssertionError("Guest Wi-Fi feature missing");
        URL url = new URL(args.length == 0 ? "https://weixin.qq.com/" : args[0]);
        InetAddress[] addresses = InetAddress.getAllByName(url.getHost());
        if (addresses.length == 0) throw new AssertionError("Guest libc DNS returned no addresses");
        System.out.println("network_dns_addresses=" + addresses.length);
        // Network.getAllByName and openConnection exercise explicit guest netId translation.
        if (network.getAllByName(url.getHost()).length == 0) throw new AssertionError("Bound DNS failed");
        verifyResponse((HttpURLConnection) url.openConnection(), "network_default_https_status");
        verifyResponse((HttpURLConnection) network.openConnection(url), "network_https_status");
        IBinder netd = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "netd");
        ClassLoader services = new dalvik.system.PathClassLoader("/system/framework/services.jar",
                NetworkCheck.class.getClassLoader());
        Object service = Class.forName("android.net.INetd$Stub", true, services)
                .getMethod("asInterface", IBinder.class).invoke(null, netd);
        if (!Boolean.TRUE.equals(Class.forName("android.net.INetd", true, services)
                .getMethod("isAlive").invoke(service)))
            throw new AssertionError("Remote guest netd Binder failed");
        System.out.println("network_complete=1");
    }

    private static void verifyResponse(HttpURLConnection request, String marker) throws Exception {
        request.setConnectTimeout(12000);
        request.setReadTimeout(12000);
        try {
            int status = request.getResponseCode();
            if (status < 200 || status >= 400) throw new AssertionError("Guest HTTP status " + status);
            if (request.getInputStream().read() < 0) throw new AssertionError("Guest response body missing");
            System.out.println(marker + "=" + status);
        } finally { request.disconnect(); }
    }

    private static void verifyWifiIdentity() throws Exception {
        java.lang.reflect.Method networkInfo = GuestConnectivity.class.getDeclaredMethod("networkInfo", org.json.JSONObject.class);
        networkInfo.setAccessible(true);
        java.lang.reflect.Method configurations = GuestWifi.class.getDeclaredMethod("configurations", org.json.JSONObject.class);
        configurations.setAccessible(true);
        for (String name : new String[] {"relay-test", "\u672c\u673a Wi-Fi", "router\\n\\x41\"end"}) {
            String ssid = "\"" + name + "\"";
            org.json.JSONObject wifi = new org.json.JSONObject().put("ssid", ssid).put("networkId", 7).put("rssi", -55);
            org.json.JSONObject host = new org.json.JSONObject().put("connected", true)
                    .put("transports", new org.json.JSONArray(new int[] {1})).put("wifi", wifi);
            if (!ssid.equals(GuestWifi.connectionInfo(host).getSSID()))
                throw new AssertionError("Host Wi-Fi SSID encoding changed: " + name);
            if (!ssid.equals(((NetworkInfo) networkInfo.invoke(null, host)).getExtraInfo()))
                throw new AssertionError("ConnectivityManager still uses a placeholder Wi-Fi name");
            NetworkCapabilities caps = GuestConnectivity.capabilities(host);
            if (!ssid.equals(NetworkCapabilities.class.getMethod("getSSID").invoke(caps)) || caps.getSignalStrength() != -55)
                throw new AssertionError("NetworkCapabilities Wi-Fi name or signal differs from WifiInfo");
            Object slice = configurations.invoke(null, wifi);
            java.util.List<?> values = (java.util.List<?>) slice.getClass().getMethod("getList").invoke(slice);
            if (values.size() != 1 || !ssid.equals(((android.net.wifi.WifiConfiguration) values.get(0)).SSID))
                throw new AssertionError("Wi-Fi configuration name differs from WifiInfo");
        }
        org.json.JSONObject redacted = new org.json.JSONObject().put("ssid", android.net.wifi.WifiManager.UNKNOWN_SSID);
        if (GuestWifi.ssid(redacted) != null)
            throw new AssertionError("Redacted Wi-Fi name was published as an actual SSID");
        org.json.JSONObject localWifi = new org.json.JSONObject().put("connected", true)
                .put("transports", new org.json.JSONArray(new int[] {0}))
                .put("wifi", new org.json.JSONObject().put("connected", true));
        if (!GuestWifi.connected(localWifi)) throw new AssertionError("Non-default Wi-Fi connection was lost");
        localWifi.getJSONObject("wifi").put("connected", false);
        if (GuestWifi.connected(localWifi)) throw new AssertionError("Disconnected Wi-Fi still shown as connected");
        System.out.println("wifi_identity_complete=1");
    }

    private static void verifyWifiConsumers() throws Exception {
        android.os.Looper.prepareMainLooper();
        Context context = new android.content.ContextWrapper(null) {
            @Override public android.content.pm.ApplicationInfo getApplicationInfo() {
                android.content.pm.ApplicationInfo info = new android.content.pm.ApplicationInfo();
                info.targetSdkVersion = 29;
                return info;
            }
            @Override public String getOpPackageName() { return "android"; }
        };
        Class<?> api = Class.forName("android.net.wifi.IWifiManager");
        android.os.Binder endpoint = LocalGuestService.endpoint(api.getName(), new GuestWifi());
        Object service = Class.forName(api.getName() + "$Stub").getMethod("asInterface", IBinder.class)
                .invoke(null, endpoint);
        android.net.wifi.WifiManager manager = (android.net.wifi.WifiManager)
                android.net.wifi.WifiManager.class.getConstructor(Context.class, api, android.os.Looper.class)
                        .newInstance(context, service, android.os.Looper.getMainLooper());
        if (manager.getScanResults() == null || !manager.getScanResults().isEmpty())
            throw new AssertionError("Guest Wi-Fi scan result contract changed");
        if (Boolean.TRUE.equals(android.net.wifi.WifiManager.class.getMethod("isWifiApEnabled").invoke(manager)))
            throw new AssertionError("Guest hotspot unexpectedly enabled");
        Object traffic = java.lang.reflect.Proxy.newProxyInstance(api.getClassLoader(),
                new Class<?>[] {Class.forName("android.net.wifi.ITrafficStateCallback")},
                (proxy, method, args) -> {
                    if (method.getName().equals("onStateChanged") && !Integer.valueOf(0).equals(args[0]))
                        throw new AssertionError("Unexpected guest traffic state");
                    return null;
                });
        LocalGuestService.call(service, api.getName(), "registerTrafficStateCallback", endpoint, traffic, 7);
        LocalGuestService.call(service, api.getName(), "unregisterTrafficStateCallback", 7);
        Object hotspot = java.lang.reflect.Proxy.newProxyInstance(api.getClassLoader(),
                new Class<?>[] {Class.forName("android.net.wifi.ISoftApCallback")},
                (proxy, method, args) -> {
                    if (method.getName().equals("onStateChanged") && !Integer.valueOf(11).equals(args[0]))
                        throw new AssertionError("Unexpected guest hotspot state");
                    if (method.getName().equals("onNumClientsChanged") && !Integer.valueOf(0).equals(args[0]))
                        throw new AssertionError("Unexpected guest hotspot clients");
                    return null;
                });
        LocalGuestService.call(service, api.getName(), "registerSoftApCallback", endpoint, hotspot, 8);
        LocalGuestService.call(service, api.getName(), "unregisterSoftApCallback", 8);
        java.lang.reflect.Field code = Class.forName(api.getName() + "$Stub").getDeclaredField("TRANSACTION_getScanResults");
        code.setAccessible(true);
        android.os.Parcel data = android.os.Parcel.obtain(), reply = android.os.Parcel.obtain();
        try {
            data.writeInterfaceToken(api.getName());
            data.writeString("android");
            if (!endpoint.transact(code.getInt(null), data, reply, 0))
                throw new AssertionError("Guest Wi-Fi scan Binder rejected");
            reply.readException();
            if (!reply.createTypedArrayList(android.net.wifi.ScanResult.CREATOR).isEmpty())
                throw new AssertionError("Guest Wi-Fi scan Binder result changed");
        } finally { data.recycle(); reply.recycle(); }
        android.os.Binder remote = new android.os.Binder() {
            @Override protected boolean onTransact(int transaction, android.os.Parcel request,
                    android.os.Parcel response, int flags) throws android.os.RemoteException {
                return endpoint.transact(transaction, request, response, flags);
            }
        };
        Object remoteService = Class.forName(api.getName() + "$Stub").getMethod("asInterface", IBinder.class)
                .invoke(null, remote);
        if (((String[]) LocalGuestService.call(remoteService, api.getName(), "getFactoryMacAddresses")).length != 0)
            throw new AssertionError("Relay invented a physical factory MAC");
        android.net.wifi.ScanResult scan = new android.net.wifi.ScanResult();
        scan.SSID = "parcel-test";
        java.util.List<android.net.wifi.ScanResult> scans = java.util.Collections.singletonList(scan);
        for (String method : new String[] {"getAllMatchingFqdnsForScanResults", "getMatchingOsuProviders"}) {
            Object matches = LocalGuestService.call(remoteService, api.getName(), method, scans);
            if (!(matches instanceof java.util.Map) || !((java.util.Map<?, ?>) matches).isEmpty())
                throw new AssertionError("Relay Passpoint map contract: " + method);
        }
        Object profiles = LocalGuestService.call(remoteService, api.getName(), "getWifiConfigsForPasspointProfiles",
                java.util.Collections.singletonList("profile-test"));
        if (!(profiles instanceof java.util.List) || !((java.util.List<?>) profiles).isEmpty())
            throw new AssertionError("Relay Passpoint string-list contract");
        Object configs = LocalGuestService.call(remoteService, api.getName(), "getMatchingPasspointConfigsForOsuProviders",
                java.util.Collections.emptyList());
        if (!(configs instanceof java.util.Map) || !((java.util.Map<?, ?>) configs).isEmpty())
            throw new AssertionError("Relay OSU map contract");
        System.out.println("wifi_settings_binder_complete=1");
        System.out.println("wifi_consumers_complete=1");
    }
}
