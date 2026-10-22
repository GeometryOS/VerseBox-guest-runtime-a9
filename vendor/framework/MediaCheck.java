package io.a10vm.guest;

import android.graphics.Bitmap;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaMetadataRetriever;
import android.media.MediaPlayer;
import android.os.IBinder;
import android.os.Looper;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class MediaCheck {
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        new Thread(() -> {
            try { verify(args); System.exit(0); }
            catch (Throwable error) { error.printStackTrace(); System.exit(1); }
        }, "media-check").start();
        Looper.loop();
    }

    private static void verify(String[] args) throws Exception {
        ProcessEvidence.report("media");
        for (String name : new String[] {"media.player", "media.extractor", "media.audio_flinger",
                "media.audio_policy", "media.resource_manager", "media.drm"}) {
            IBinder service = (IBinder) Class.forName("android.os.ServiceManager")
                    .getMethod("checkService", String.class).invoke(null, name);
            if (service == null || !service.isBinderAlive()) throw new AssertionError("Media service missing: " + name);
            System.out.println("media_service=" + name);
        }
        MediaPlayer playerCheck = new MediaPlayer();
        try { System.out.println("media_player_jni=1"); }
        finally { playerCheck.release(); }
        int decoders = 0;
        boolean avc = false, aac = false;
        for (MediaCodecInfo codec : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
            if (codec.isEncoder()) continue;
            decoders++;
            for (String type : codec.getSupportedTypes()) {
                avc |= "video/avc".equals(type);
                aac |= "audio/mp4a-latm".equals(type);
            }
        }
        if (!avc || !aac) throw new AssertionError("AVC/AAC decoder missing");
        System.out.println("media_decoders=" + decoders);
        if (args.length > 0) {
            MediaMetadataRetriever metadata = new MediaMetadataRetriever();
            try {
                metadata.setDataSource(args[0]);
                Bitmap frame = metadata.getFrameAtTime(0);
                if (frame == null || frame.getWidth() <= 0 || frame.getHeight() <= 0)
                    throw new AssertionError("Decoded video frame missing");
                System.out.println("media_video_frame=" + frame.getWidth() + "x" + frame.getHeight());
                frame.recycle();
            } finally { metadata.release(); }
            CountDownLatch finished = new CountDownLatch(1);
            AtomicReference<String> failure = new AtomicReference<>();
            MediaPlayer player = new MediaPlayer();
            try {
                player.setOnCompletionListener(value -> finished.countDown());
                player.setOnErrorListener((value, what, extra) -> {
                    failure.set(what + ":" + extra); finished.countDown(); return true;
                });
                player.setDataSource(args[0]);
                player.prepare();
                player.start();
                if (!finished.await(20, TimeUnit.SECONDS)) throw new AssertionError("Media playback timeout");
                if (failure.get() != null) throw new AssertionError("Media playback error: " + failure.get());
                System.out.println("media_playback_completed=1");
            } finally { player.release(); }
        }
        System.out.println("media_complete=1");
    }
}
