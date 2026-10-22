package io.a10vm.guest;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Vibrator;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class DeviceCheck {
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        Class<?> activity = Class.forName("android.app.ActivityThread");
        Context context = (Context) activity.getMethod("getSystemContext")
                .invoke(activity.getMethod("systemMain").invoke(null));
        new Thread(() -> {
            try { verify(context, args); System.exit(0); }
            catch (Throwable error) { error.printStackTrace(); System.exit(1); }
        }, "device-check").start();
        Looper.loop();
    }

    private static void verify(Context context, String[] args) throws Exception {
        ProcessEvidence.report("device");
        BatteryManager battery = context.getSystemService(BatteryManager.class);
        int level = battery.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
        if (level < 0 || level > 100) throw new AssertionError("Guest battery capacity invalid: " + level);
        android.os.IBinder binder = (android.os.IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "activity");
        Object activity = Class.forName("android.app.IActivityManager$Stub")
                .getMethod("asInterface", android.os.IBinder.class).invoke(null, binder);
        // A shell check has no ActivityManager application record; read the sticky with no caller.
        Intent info = (Intent) LocalGuestService.call(activity, "android.app.IActivityManager", "registerReceiver",
                null, "com.android.shell", null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED), null, 0, 0);
        if (info == null) throw new AssertionError("Guest battery broadcast missing");
        System.out.println("device_battery_level=" + level);
        System.out.println("device_battery_status=" + info.getIntExtra(BatteryManager.EXTRA_STATUS, -1));
        Vibrator vibrator = context.getSystemService(Vibrator.class);
        if (!vibrator.hasVibrator()) throw new AssertionError("Guest vibrator missing");
        System.out.println("device_vibrator=1");
        if (java.util.Arrays.asList(args).contains("--vibrate")) {
            vibrator.vibrate(android.os.VibrationEffect.createOneShot(20, -1));
            System.out.println("device_vibration_requested=1");
        }
        SensorManager manager = context.getSystemService(SensorManager.class);
        int count = manager.getSensorList(Sensor.TYPE_ALL).size();
        if (count == 0) throw new AssertionError("Guest sensors missing");
        System.out.println("device_sensors=" + count);
        Sensor sensor = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        if (sensor == null) throw new AssertionError("Guest accelerometer missing");
        CountDownLatch event = new CountDownLatch(1);
        SensorEventListener listener = new SensorEventListener() {
            @Override public void onSensorChanged(SensorEvent sample) {
                if (sample.timestamp > 0 && sample.values.length == 3) event.countDown();
            }
            @Override public void onAccuracyChanged(Sensor sensor, int accuracy) { }
        };
        try {
            if (!manager.registerListener(listener, sensor, 20000, new Handler(Looper.getMainLooper())))
                throw new AssertionError("Guest sensor activation failed");
            if (!event.await(5, TimeUnit.SECONDS)) throw new AssertionError("Guest sensor event missing");
            System.out.println("device_accelerometer_event=1");
        } finally { manager.unregisterListener(listener); }
        System.out.println("device_complete=1");
    }
}
