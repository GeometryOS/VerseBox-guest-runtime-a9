package io.a10vm.guest;

public final class CameraQueryCheck {
    @SuppressWarnings("deprecation")
    public static void main(String[] args) {
        long began = android.os.SystemClock.elapsedRealtime();
        int count = android.hardware.Camera.getNumberOfCameras();
        long elapsed = android.os.SystemClock.elapsedRealtime() - began;
        System.out.println("camera_query_count=" + count);
        System.out.println("camera_query_millis=" + elapsed);
        if (count == 0 || elapsed > 1000) throw new AssertionError("Guest camera enumeration failed or stalled");
        System.out.println("camera_query_ready=1");
    }
}
