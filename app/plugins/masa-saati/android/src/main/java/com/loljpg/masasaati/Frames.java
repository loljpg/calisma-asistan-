package com.loljpg.masasaati;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Masa saatiyle konuşma biçimi (docs/bluetooth.md).
 * Çerçeve: 0xA5, tür (1 bayt, ASCII), uzunluk (2 bayt, küçük uçlu), veri.
 * Çerçeveler BLE paketlerine bölünerek gönderilir; karşı taraf akışı yeniden birleştirir.
 */
final class Frames {

    static final int MAGIC = 0xA5;
    static final int MAX = 16000;
    /** Simgelerde saydam piksellerin yerine yazılan renk (RGB565 eflatun). */
    static final int TRANSPARENT = 0xF81F;

    private Frames() {}

    static byte[] encode(char type, byte[] payload) {
        byte[] f = new byte[4 + payload.length];
        f[0] = (byte) MAGIC;
        f[1] = (byte) type;
        f[2] = (byte) (payload.length & 0xFF);
        f[3] = (byte) ((payload.length >> 8) & 0xFF);
        System.arraycopy(payload, 0, f, 4, payload.length);
        return f;
    }

    static byte[] utf8(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    interface Sink {
        void frame(char type, byte[] payload);
    }

    /** Gelen baytları biriktirip tam çerçeveleri çıkarır. */
    static final class Reader {

        private byte[] buf = new byte[1024];
        private int n = 0;

        void reset() {
            n = 0;
        }

        void feed(byte[] data, Sink sink) {
            if (n + data.length > buf.length) buf = Arrays.copyOf(buf, Math.max(buf.length * 2, n + data.length));
            System.arraycopy(data, 0, buf, n, data.length);
            n += data.length;
            int i = 0;
            while (true) {
                while (i < n && (buf[i] & 0xFF) != MAGIC) i++;
                if (n - i < 4) break;
                int len = (buf[i + 2] & 0xFF) | ((buf[i + 3] & 0xFF) << 8);
                if (len > MAX) {
                    i++;
                    continue;
                }
                if (n - i < 4 + len) break;
                byte[] p = Arrays.copyOfRange(buf, i + 4, i + 4 + len);
                char type = (char) (buf[i + 1] & 0xFF);
                i += 4 + len;
                sink.frame(type, p);
            }
            System.arraycopy(buf, i, buf, 0, n - i);
            n -= i;
            if (buf.length > 8192 && n < 1024) buf = Arrays.copyOf(buf, 1024);
        }
    }

    static int rgb565(int argb) {
        int r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    /** Bitmap'i w×h boyutuna kırpıp küçültür (ortadan). */
    static Bitmap cropScale(Bitmap src, int w, int h) {
        int sw = src.getWidth(), sh = src.getHeight();
        float scale = Math.max((float) w / sw, (float) h / sh);
        int cw = Math.round(w / scale), ch = Math.round(h / scale);
        int cx = Math.max(0, (sw - cw) / 2), cy = Math.max(0, (sh - ch) / 2);
        Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        c.drawBitmap(src, new Rect(cx, cy, cx + Math.min(cw, sw), cy + Math.min(ch, sh)), new Rect(0, 0, w, h), null);
        return out;
    }

    static Bitmap drawable(Drawable d, int w, int h) {
        Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        d.setBounds(0, 0, w, h);
        d.draw(c);
        return out;
    }

    /**
     * Görüntü çerçevesi: kimlik (4 bayt), genişlik ve yükseklik (2'şer bayt), ardından
     * küçük uçlu RGB565 pikseller. transparent: yarıdan saydam pikseller TRANSPARENT olur.
     */
    static byte[] image(long id, Bitmap bm, boolean transparent) {
        int w = bm.getWidth(), h = bm.getHeight();
        int[] px = new int[w * h];
        bm.getPixels(px, 0, w, 0, 0, w, h);
        byte[] out = new byte[8 + px.length * 2];
        out[0] = (byte) (id & 0xFF);
        out[1] = (byte) ((id >> 8) & 0xFF);
        out[2] = (byte) ((id >> 16) & 0xFF);
        out[3] = (byte) ((id >> 24) & 0xFF);
        out[4] = (byte) (w & 0xFF);
        out[5] = (byte) ((w >> 8) & 0xFF);
        out[6] = (byte) (h & 0xFF);
        out[7] = (byte) ((h >> 8) & 0xFF);
        for (int i = 0; i < px.length; i++) {
            int a = (px[i] >>> 24) & 0xFF;
            int v;
            if (transparent && a < 128) {
                v = TRANSPARENT;
            } else {
                v = rgb565(px[i]);
                if (transparent && v == TRANSPARENT) v = 0xF81E;
            }
            out[8 + i * 2] = (byte) (v & 0xFF);
            out[9 + i * 2] = (byte) ((v >> 8) & 0xFF);
        }
        return out;
    }
}
