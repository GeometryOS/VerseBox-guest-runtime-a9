package io.a10vm.guest;

import java.util.Iterator;
import java.util.Map;
import android.content.pm.FeatureInfo;

final class GuestCapabilities {
    static void configure() throws Exception {
        Class<?> config = Class.forName("com.android.server.SystemConfig");
        Object instance = config.getMethod("getInstance").invoke(null);
        @SuppressWarnings("unchecked")
        Map<String, FeatureInfo> features = (Map<String, FeatureInfo>) config.getMethod("getAvailableFeatures").invoke(instance);
        Iterator<?> names = features.keySet().iterator();
        while (names.hasNext()) {
            String name = (String) names.next();
            if (name.startsWith("android.hardware.") && !name.startsWith("android.hardware.touchscreen") &&
                    !name.startsWith("android.hardware.faketouch") && !name.startsWith("android.hardware.screen."))
                names.remove();
        }
        for (String name : new String[] {"android.hardware.audio.output", "android.hardware.microphone",
                "android.hardware.wifi"}) {
            FeatureInfo feature = new FeatureInfo();
            feature.name = name;
            features.put(name, feature);
        }
        java.nio.file.Path snapshot = java.nio.file.Paths.get(System.getenv("A10VM_STATE_ROOT"),
                "host-device-capabilities.json");
        if (java.nio.file.Files.isRegularFile(snapshot)) {
            org.json.JSONArray types = new org.json.JSONObject(new String(java.nio.file.Files.readAllBytes(snapshot),
                    java.nio.charset.StandardCharsets.UTF_8)).getJSONArray("sensorTypes");
            Map<Integer, String> sensorFeatures = new java.util.HashMap<>();
            sensorFeatures.put(1, "accelerometer"); sensorFeatures.put(2, "compass");
            sensorFeatures.put(4, "gyroscope"); sensorFeatures.put(5, "light");
            sensorFeatures.put(6, "barometer"); sensorFeatures.put(8, "proximity");
            sensorFeatures.put(12, "relative_humidity"); sensorFeatures.put(13, "ambient_temperature");
            for (int i = 0; i < types.length(); i++) {
                String suffix = sensorFeatures.get(types.getInt(i));
                if (suffix == null) continue;
                FeatureInfo feature = new FeatureInfo();
                feature.name = "android.hardware.sensor." + suffix;
                features.put(feature.name, feature);
            }
        }
        java.nio.file.Path cameras = java.nio.file.Paths.get(System.getenv("A10VM_STATE_ROOT"),
                "host-camera-capabilities.json");
        if (java.nio.file.Files.isRegularFile(cameras)) {
            org.json.JSONArray facing = new org.json.JSONArray(new String(java.nio.file.Files.readAllBytes(cameras),
                    java.nio.charset.StandardCharsets.UTF_8));
            java.util.Set<String> namesToAdd = new java.util.HashSet<>();
            if (facing.length() > 0) namesToAdd.add("android.hardware.camera.any");
            for (int i = 0; i < facing.length(); ++i)
                namesToAdd.add(facing.getInt(i) == 1 ? "android.hardware.camera.front" : "android.hardware.camera");
            for (String name : namesToAdd) {
                FeatureInfo feature = new FeatureInfo(); feature.name = name;
                features.put(name, feature);
            }
        }
    }
}
