package io.a10vm.guest;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Array;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

final class GuestDnsResolver implements InvocationHandler {
    private final Map<Integer, Object> configurations = new HashMap<>();
    static void register() throws Exception {
        LocalGuestService.register("dnsresolver", "android.net.IDnsResolver", new GuestDnsResolver());
    }

    @Override public synchronized Object invoke(Object proxy, Method method, Object[] args) throws Exception {
        switch (method.getName()) {
            case "isAlive": return true;
            case "getInterfaceVersion": return Class.forName("android.net.IDnsResolver").getField("VERSION").getInt(null);
            case "registerEventListener": return null;
            case "createNetworkCache": configurations.put((Integer) args[0], null); return null;
            case "destroyNetworkCache": configurations.remove((Integer) args[0]); return null;
            case "setResolverConfiguration":
                Object params = args[0];
                int id = params.getClass().getField("netId").getInt(params);
                configurations.put(id, params);
                return null;
            case "getResolverInfo":
                Object configuration = configurations.get((Integer) args[0]);
                if (configuration != null) {
                    copy(configuration.getClass().getField("servers").get(configuration), args[1]);
                    copy(configuration.getClass().getField("domains").get(configuration), args[2]);
                    copy(configuration.getClass().getField("tlsServers").get(configuration), args[3]);
                    String[] fields = {"sampleValiditySeconds", "successThreshold", "minSamples", "maxSamples",
                                       "baseTimeoutMsec", "retryCount"};
                    int[] values = new int[fields.length];
                    for (int i = 0; i < fields.length; i++)
                        values[i] = configuration.getClass().getField(fields[i]).getInt(configuration);
                    copy(values, args[4]);
                } else {
                    org.json.JSONObject network = GuestConnectivity.snapshot();
                    org.json.JSONArray servers = network.optJSONArray("dns");
                    if (servers != null && args[1] != null)
                        for (int i = 0; i < Math.min(servers.length(), Array.getLength(args[1])); i++)
                            Array.set(args[1], i, servers.getString(i));
                    String domains = network.optString("domains");
                    if (!domains.isEmpty()) copy(domains.split("\\s+"), args[2]);
                }
                if (args[5] != null) Arrays.fill((int[]) args[5], 0);
                if (args[6] != null) Arrays.fill((int[]) args[6], 0);
                return null;
            case "getPrefix64":
                String prefix = GuestConnectivity.snapshot().optString("nat64");
                if (!prefix.isEmpty()) return prefix;
                throw GuestNetworks.error(61, "Host network has no NAT64 prefix");
            case "startPrefix64Discovery": case "stopPrefix64Discovery":
                // Discovery belongs to the host network, whose LinkProperties carries its result.
                return null;
            default: throw GuestNetworks.error(95, "Host resolver operation: " + method.getName());
        }
    }

    private static void copy(Object source, Object destination) {
        if (source != null && destination != null)
            System.arraycopy(source, 0, destination, 0, Math.min(Array.getLength(source), Array.getLength(destination)));
    }
}
