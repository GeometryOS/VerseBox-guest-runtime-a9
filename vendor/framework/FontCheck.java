package io.a10vm.guest;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Build;

public final class FontCheck {
    public static void main(String[] args) throws Exception {
        if (Build.VERSION.SDK_INT != 29) throw new AssertionError("Wrong guest SDK");
        GuestApplication.configureRuntime(29);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setColor(Color.WHITE);
        paint.setTextSize(24);
        Paint.FontMetrics metrics = paint.getFontMetrics();
        if (metrics.ascent >= 0 || metrics.descent <= 0 || paint.measureText("Android 10") <= 0)
            throw new AssertionError("Guest default font metrics");
        Bitmap bitmap = Bitmap.createBitmap(240, 48, Bitmap.Config.ARGB_8888);
        new Canvas(bitmap).drawText("Android 10", 4, 32, paint);
        int pixels = 0;
        for (int y = 0; y < bitmap.getHeight(); ++y)
            for (int x = 0; x < bitmap.getWidth(); ++x)
                if (Color.alpha(bitmap.getPixel(x, y)) != 0) ++pixels;
        if (pixels < 100) throw new AssertionError("Guest default font rendered no text");
        System.out.println("guest_font_pixels=" + pixels);
        System.out.println("guest_fonts_ready=1");
    }
}
