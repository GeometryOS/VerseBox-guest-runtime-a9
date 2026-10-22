package io.a10vm.guest;

import java.lang.reflect.Method;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;

public final class GuestServiceApiCheck {
    public static void main(String[] args) throws Exception {
        JSONObject result = new JSONObject();
        ClassLoader services = new dalvik.system.PathClassLoader("/system/framework/services.jar",
                GuestServiceApiCheck.class.getClassLoader());
        for (String type : new String[] {"android.security.keystore.IKeystoreService", "android.os.IVold",
                "android.os.IStoraged", "android.net.INetd", "android.net.wifi.IWifiManager"}) {
            Method[] methods = Class.forName(type, true, services).getDeclaredMethods();
            Arrays.sort(methods, (left, right) -> left.getName().compareTo(right.getName()));
            JSONArray entries = new JSONArray();
            for (Method method : methods) {
                JSONArray parameters = new JSONArray();
                for (Class<?> parameter : method.getParameterTypes()) parameters.put(parameter.getName());
                entries.put(new JSONObject().put("name", method.getName())
                        .put("result", method.getReturnType().getName()).put("parameters", parameters));
            }
            result.put(type, entries);
        }
        System.out.println("guest_service_api=" + result);
    }
}
