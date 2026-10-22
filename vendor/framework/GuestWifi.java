package io.a10vm.guest;

import android.net.wifi.SupplicantState;
import android.net.wifi.WifiInfo;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.Collections;
import java.util.ArrayList;
import java.util.List;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

/** Host Wi-Fi state relay; radio changes are presented in the host's system panel. */
final class GuestWifi implements InvocationHandler {
    private volatile int verboseLogging;
    private static int previousState = -1;
    private static String previousNetwork = "";

    static void register() throws Exception {
        LocalGuestService.register("wifi", "android.net.wifi.IWifiManager", new GuestWifi());
    }

    static WifiInfo connectionInfo(JSONObject snapshot) throws Exception {
        JSONObject wifi = snapshot.optJSONObject("wifi");
        if (wifi == null) wifi = new JSONObject();
        WifiInfo info = ssidInfo(wifi);
        WifiInfo.class.getMethod("setNetworkId", int.class).invoke(info, wifi.optInt("networkId", -1));
        WifiInfo.class.getMethod("setRssi", int.class).invoke(info, wifi.optInt("rssi", -127));
        WifiInfo.class.getMethod("setLinkSpeed", int.class).invoke(info, wifi.optInt("linkSpeed", -1));
        WifiInfo.class.getMethod("setFrequency", int.class).invoke(info, wifi.optInt("frequency", -1));
        WifiInfo.class.getMethod("setBSSID", String.class).invoke(info, wifi.optString("bssid", null));
        WifiInfo.class.getMethod("setMacAddress", String.class).invoke(info,
                wifi.optString("mac", "02:00:00:00:00:00"));
        WifiInfo.class.getMethod("setSupplicantState", SupplicantState.class).invoke(info,
                connected(snapshot) ?
                        SupplicantState.COMPLETED : SupplicantState.DISCONNECTED);
        int ip = wifi.optInt("ip");
        if (ip != 0) WifiInfo.class.getMethod("setInetAddress", InetAddress.class).invoke(info,
                InetAddress.getByAddress(new byte[] {(byte) ip, (byte) (ip >> 8), (byte) (ip >> 16), (byte) (ip >> 24)}));
        return info;
    }

    private static WifiInfo ssidInfo(JSONObject wifi) throws Exception {
        WifiInfo info = WifiInfo.class.getConstructor().newInstance();
        String ssid = wifi == null ? WifiManager.UNKNOWN_SSID : wifi.optString("ssid", WifiManager.UNKNOWN_SSID);
        Class<?> type = Class.forName("android.net.wifi.WifiSsid");
        Object value = null;
        if (!WifiManager.UNKNOWN_SSID.equals(ssid)) {
            if (ssid.startsWith("0x")) value = type.getMethod("createFromHex", String.class).invoke(null, ssid);
            else {
                if (ssid.length() >= 2 && ssid.startsWith("\"") && ssid.endsWith("\""))
                    ssid = ssid.substring(1, ssid.length() - 1);
                value = type.getMethod("createFromByteArray", byte[].class).invoke(null,
                        (Object) ssid.getBytes(StandardCharsets.UTF_8));
            }
        }
        WifiInfo.class.getMethod("setSSID", type).invoke(info, value);
        return info;
    }

    static String ssid(JSONObject wifi) throws Exception {
        String ssid = ssidInfo(wifi).getSSID();
        return WifiManager.UNKNOWN_SSID.equals(ssid) ? null : ssid;
    }

    static boolean connected(JSONObject snapshot) {
        JSONObject wifi = snapshot.optJSONObject("wifi");
        if (wifi != null && wifi.has("connected")) return wifi.optBoolean("connected");
        JSONArray transports = snapshot.optJSONArray("transports");
        if (snapshot.optBoolean("connected") && transports != null)
            for (int i = 0; i < transports.length(); i++) if (transports.optInt(i) == 1) return true;
        return false;
    }

    private static List<ScanResult> scans(JSONObject wifi) throws Exception {
        List<ScanResult> results = new ArrayList<>();
        JSONArray values = wifi.optJSONArray("scans");
        for (int i = 0; values != null && i < values.length(); i++) {
            JSONObject value = values.getJSONObject(i);
            ScanResult scan = new ScanResult();
            scan.SSID = value.getString("ssid"); scan.BSSID = value.getString("bssid");
            scan.frequency = value.getInt("frequency"); scan.level = value.getInt("level");
            scan.timestamp = value.getLong("timestamp"); scan.capabilities = value.getString("capabilities");
            results.add(scan);
        }
        return results;
    }

    private static Object configurations(JSONObject wifi) throws Exception {
        List<WifiConfiguration> values = new ArrayList<>();
        String ssid = ssid(wifi);
        if (ssid != null && wifi.optInt("networkId", -1) >= 0) {
            WifiConfiguration config = new WifiConfiguration();
            config.SSID = ssid;
            config.networkId = wifi.getInt("networkId");
            config.status = WifiConfiguration.Status.CURRENT;
            int security = wifi.optInt("security", -1);
            config.allowedKeyManagement.clear();
            if (security == 0) config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
            else if (security == 2) config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);
            else if (security == 3 || security == 5) config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_EAP);
            else if (security == 4) config.allowedKeyManagement.set(8); // SAE in Android 10.
            else if (security == 6) config.allowedKeyManagement.set(9); // OWE in Android 10.
            values.add(config);
        }
        return Class.forName("android.content.pm.ParceledListSlice").getConstructor(java.util.List.class)
                .newInstance(values);
    }

    private static boolean request(String action) throws Exception {
        java.nio.file.Path pending = Paths.get(System.getenv("A10VM_STATE_ROOT"), "host-wifi-action.tmp");
        java.nio.file.Path target = pending.resolveSibling("host-wifi-action.json");
        Files.write(pending, new JSONObject().put("action", action).toString().getBytes(StandardCharsets.UTF_8));
        Files.move(pending, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        return true;
    }

    static synchronized void publish(JSONObject snapshot) throws Exception {
        JSONObject wifi = snapshot.optJSONObject("wifi");
        if (wifi == null) return;
        Class<?> activity = Class.forName("android.app.ActivityThread");
        Object thread = activity.getMethod("currentActivityThread").invoke(null);
        android.content.Context context = (android.content.Context) activity.getMethod("getSystemContext").invoke(thread);
        int state = wifi.optInt("state", WifiManager.WIFI_STATE_DISABLED);
        if (state != previousState || snapshot.has("wifiAction")) {
            android.content.Intent intent = new android.content.Intent(WifiManager.WIFI_STATE_CHANGED_ACTION)
                    .putExtra(WifiManager.EXTRA_WIFI_STATE, state)
                    .putExtra(WifiManager.EXTRA_PREVIOUS_WIFI_STATE, previousState < 0 ? state : previousState);
            broadcast(context, intent, true);
            previousState = state;
        }
        String signature = wifi.toString() + connected(snapshot);
        if (!signature.equals(previousNetwork)) {
            android.net.NetworkInfo info = android.net.NetworkInfo.class.getConstructor(int.class, int.class,
                    String.class, String.class).newInstance(1, 0, "WIFI", "");
            android.net.NetworkInfo.class.getMethod("setIsAvailable", boolean.class).invoke(info, state == 3);
            android.net.NetworkInfo.class.getMethod("setDetailedState", android.net.NetworkInfo.DetailedState.class,
                    String.class, String.class).invoke(info, connected(snapshot) ?
                        android.net.NetworkInfo.DetailedState.CONNECTED : android.net.NetworkInfo.DetailedState.DISCONNECTED,
                        null, connected(snapshot) ? ssid(wifi) : null);
            broadcast(context, new android.content.Intent(WifiManager.NETWORK_STATE_CHANGED_ACTION)
                    .putExtra(WifiManager.EXTRA_NETWORK_INFO, info)
                    .putExtra(WifiManager.EXTRA_WIFI_INFO, connectionInfo(snapshot)), true);
            broadcast(context, new android.content.Intent(WifiManager.RSSI_CHANGED_ACTION)
                    .putExtra(WifiManager.EXTRA_NEW_RSSI, wifi.optInt("rssi", -127)), false);
            previousNetwork = signature;
        }
        if (snapshot.has("scanUpdated")) broadcast(context, new android.content.Intent(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)
                .putExtra(WifiManager.EXTRA_RESULTS_UPDATED, snapshot.optBoolean("scanUpdated")), false);
    }

    private static void broadcast(android.content.Context context, android.content.Intent intent, boolean sticky) throws Exception {
        Object all = android.os.UserHandle.class.getField("ALL").get(null);
        android.content.Context.class.getMethod(sticky ? "sendStickyBroadcastAsUser" : "sendBroadcastAsUser",
                android.content.Intent.class, android.os.UserHandle.class).invoke(context, intent, all);
    }

    @Override public Object invoke(Object proxy, Method method, Object[] args) throws Exception {
        JSONObject snapshot = GuestConnectivity.snapshot();
        JSONObject wifi = snapshot.optJSONObject("wifi");
        if (wifi == null) wifi = new JSONObject();
        switch (method.getName()) {
            case "getVerboseLoggingLevel": return verboseLogging;
            case "enableVerboseLogging": verboseLogging = Math.max(0, (Integer) args[0]); return null;
            case "getConnectionInfo": return connectionInfo(snapshot);
            case "getWifiEnabledState": return wifi.optInt("state", WifiManager.WIFI_STATE_DISABLED);
            case "getSupportedFeatures": return 1L | (wifi.optBoolean("dualBand") ? 2L : 0L);
            // MIUI's developer switch controls a physical radio extension absent from the relay.
            case "isWifiCoverageExtendFeatureEnabled": return false;
            case "enableWifiCoverageExtendFeature": return null;
            case "getScanResults": return scans(wifi);
            case "getFactoryMacAddresses": return new String[0];
            case "getAllMatchingFqdnsForScanResults": case "getMatchingOsuProviders":
            case "getMatchingPasspointConfigsForOsuProviders": return Collections.emptyMap();
            case "getWifiConfigsForPasspointProfiles": return Collections.emptyList();
            case "getConfiguredNetworks": case "getPrivilegedConfiguredNetworks":
                return configurations(wifi);
            case "startScan": return request("scan");
            case "setWifiEnabled": return request("panel");
            case "getDhcpInfo":
                android.net.DhcpInfo dhcp = new android.net.DhcpInfo();
                JSONObject lease = wifi.optJSONObject("dhcp");
                if (lease == null) lease = new JSONObject();
                dhcp.ipAddress = lease.optInt("ipAddress", wifi.optInt("ip"));
                dhcp.gateway = lease.optInt("gateway"); dhcp.netmask = lease.optInt("netmask");
                dhcp.dns1 = lease.optInt("dns1"); dhcp.dns2 = lease.optInt("dns2");
                dhcp.serverAddress = lease.optInt("serverAddress"); dhcp.leaseDuration = lease.optInt("leaseDuration");
                return dhcp;
            case "acquireWifiLock": case "releaseWifiLock": case "disconnect": return false;
            case "getWifiServiceMessenger": return GuestWifiChannel.get();
            case "isScanAlwaysAvailable": return false;
            case "is5GHzBandSupported": return wifi.optBoolean("dualBand");
            // The relay has no hotspot or traffic-activity feed. Publish their initial idle state.
            case "getWifiApEnabledState": return 11; // WIFI_AP_STATE_DISABLED
            case "getWifiApConfiguration": return null;
            case "registerTrafficStateCallback":
                LocalGuestService.call(args[1], "android.net.wifi.ITrafficStateCallback", "onStateChanged", 0);
                return null;
            case "registerSoftApCallback":
                LocalGuestService.call(args[1], "android.net.wifi.ISoftApCallback", "onStateChanged", 11, 0);
                LocalGuestService.call(args[1], "android.net.wifi.ISoftApCallback", "onNumClientsChanged", 0);
                return null;
            case "unregisterTrafficStateCallback": case "unregisterSoftApCallback": return null;
            case "reportActivityInfo": return null;
            case "requestActivityInfo":
                if (args[0] != null) ((android.os.ResultReceiver) args[0]).send(0, null);
                return null;
            case "getCurrentNetwork":
                if (!connected(snapshot)) return null;
                String path = java.nio.file.Paths.get(System.getenv("A10VM_STATE_ROOT"), "guest-network-id").toString();
                if (!java.nio.file.Files.isRegularFile(java.nio.file.Paths.get(path))) return null;
                int id = Integer.parseInt(ProcessEvidence.read(path).trim());
                return Class.forName("android.net.Network").getConstructor(int.class).newInstance(id);
            default: throw new UnsupportedOperationException("Host Wi-Fi relay: " + method.getName());
        }
    }
}
