package io.a10vm.guest;

import android.media.MediaPlayer;

public final class MediaResetCheck {
    public static void main(String[] args) throws Exception {
        for (int i = 0; i < 2; ++i) {
            MediaPlayer player = new MediaPlayer();
            try {
                player.setDataSource("/system/media/audio/ui/Unlock.ogg");
                player.prepare();
                player.reset();
            } finally { player.release(); }
        }
        System.out.println("media_prepared_reset_complete=1");
    }
}
