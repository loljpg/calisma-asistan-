package com.loljpg.masasaati;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.ContentObserver;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import androidx.palette.graphics.Palette;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Telefonda çalan müziği (Spotify, YouTube Music…) izler, masa saatine gönderir ve
 * masa saatinden gelen komutları (oynat/durdur, atla, ses) uygular.
 * Bildirim erişimi izni gerekir (MediaSessionManager bunu ister).
 */
public final class MediaBridge {

    private static MediaBridge inst;

    public static synchronized MediaBridge get(Context c) {
        if (inst == null) inst = new MediaBridge(c.getApplicationContext());
        return inst;
    }

    private final Context ctx;
    private final Handler h = new Handler(Looper.getMainLooper());
    private MediaSessionManager msm;
    private MediaController ctrl;
    private boolean attached;
    private long artKey = 0;
    private Bitmap artSmall;
    private int[] accent, dark;

    private MediaBridge(Context c) {
        ctx = c;
    }

    private ComponentName comp() {
        return new ComponentName(ctx, NotifListener.class);
    }

    void attach() {
        h.post(() -> {
            if (attached) return;
            try {
                msm = (MediaSessionManager) ctx.getSystemService(Context.MEDIA_SESSION_SERVICE);
                msm.addOnActiveSessionsChangedListener(sessions, comp(), h);
                attached = true;
                pick(msm.getActiveSessions(comp()));
                ctx.getContentResolver().registerContentObserver(Settings.System.CONTENT_URI, true, volumeObserver);
            } catch (Exception ignored) {
                attached = false;
            }
        });
    }

    void detach() {
        h.post(() -> {
            try {
                if (msm != null) msm.removeOnActiveSessionsChangedListener(sessions);
                ctx.getContentResolver().unregisterContentObserver(volumeObserver);
            } catch (Exception ignored) {}
            setController(null);
            attached = false;
        });
    }

    private final MediaSessionManager.OnActiveSessionsChangedListener sessions = this::pick;

    private final ContentObserver volumeObserver = new ContentObserver(new Handler(Looper.getMainLooper())) {
        @Override
        public void onChange(boolean selfChange) {
            schedule();
        }
    };

    private void pick(List<MediaController> list) {
        MediaController best = null;
        if (list != null) {
            for (MediaController c : list) {
                PlaybackState st = c.getPlaybackState();
                if (st != null && st.getState() == PlaybackState.STATE_PLAYING) {
                    best = c;
                    break;
                }
            }
            if (best == null && !list.isEmpty()) best = list.get(0);
        }
        setController(best);
        schedule();
    }

    private void setController(MediaController c) {
        if (ctrl != null && (c == null || !c.getSessionToken().equals(ctrl.getSessionToken()))) {
            try {
                ctrl.unregisterCallback(callback);
            } catch (Exception ignored) {}
        }
        if (c != null && (ctrl == null || !c.getSessionToken().equals(ctrl.getSessionToken()))) {
            c.registerCallback(callback, h);
        }
        ctrl = c;
    }

    private final MediaController.Callback callback = new MediaController.Callback() {
        @Override
        public void onMetadataChanged(MediaMetadata metadata) {
            schedule();
        }

        @Override
        public void onPlaybackStateChanged(PlaybackState state) {
            schedule();
        }

        @Override
        public void onSessionDestroyed() {
            h.post(() -> {
                setController(null);
                try {
                    if (msm != null) pick(msm.getActiveSessions(comp()));
                } catch (Exception e) {
                    schedule();
                }
            });
        }
    };

    private final Runnable sendNow = this::send;

    private void schedule() {
        h.removeCallbacks(sendNow);
        h.postDelayed(sendNow, 250);
    }

    /** Masa saati yeniden bağlanınca ya da ayar değişince son durumu tekrar gönderir. */
    void resend() {
        h.post(() -> {
            artKey = 0;
            schedule();
        });
    }

    private static JSONArray rgb(int[] c) {
        JSONArray a = new JSONArray();
        a.put(c[0]).put(c[1]).put(c[2]);
        return a;
    }

    private void send() {
        BleLink link = BleLink.get(ctx);
        if (!link.isAuthed()) return;
        try {
            JSONObject o = new JSONObject();
            boolean enabled = link.prefs.getBoolean("media", true);
            MediaMetadata md = ctrl == null ? null : ctrl.getMetadata();
            if (!enabled || ctrl == null || md == null) {
                o.put("on", false);
                o.put("enabled", enabled);
                link.sendMedia(o, 0, null);
                return;
            }
            String title = md.getString(MediaMetadata.METADATA_KEY_TITLE);
            if (title == null) title = md.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE);
            String artist = md.getString(MediaMetadata.METADATA_KEY_ARTIST);
            if (artist == null) artist = md.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST);
            if (artist == null) artist = md.getString(MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE);
            String album = md.getString(MediaMetadata.METADATA_KEY_ALBUM);
            long dur = md.getLong(MediaMetadata.METADATA_KEY_DURATION);
            PlaybackState st = ctrl.getPlaybackState();
            boolean playing = st != null && st.getState() == PlaybackState.STATE_PLAYING;
            long pos = 0;
            if (st != null) {
                pos = st.getPosition();
                if (playing && st.getLastPositionUpdateTime() > 0) {
                    pos += (long) ((SystemClock.elapsedRealtime() - st.getLastPositionUpdateTime()) * st.getPlaybackSpeed());
                }
            }
            String pkg = ctrl.getPackageName();
            String app = pkg;
            try {
                PackageManager pm = ctx.getPackageManager();
                app = pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString();
            } catch (Exception ignored) {}

            // Kapak: şarkı değişince küçültülür, renkleri çıkarılır.
            long key = ((long) ((title == null ? "" : title) + "|" + artist + "|" + album + "|" + pkg).hashCode()) & 0xFFFFFFFFL;
            if (key == 0) key = 1;
            if (key != artKey) {
                artKey = key;
                artSmall = null;
                accent = dark = null;
                Bitmap art = md.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART);
                if (art == null) art = md.getBitmap(MediaMetadata.METADATA_KEY_ART);
                if (art == null) art = md.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON);
                if (art != null) {
                    artSmall = Frames.cropScale(art, 80, 80);
                    colors(art);
                }
            }

            AudioManager am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
            int vol = am.getStreamVolume(AudioManager.STREAM_MUSIC), volMax = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
            MediaController.PlaybackInfo pi = ctrl.getPlaybackInfo();
            if (pi != null && pi.getPlaybackType() == MediaController.PlaybackInfo.PLAYBACK_TYPE_REMOTE) {
                vol = pi.getCurrentVolume();
                volMax = pi.getMaxVolume();
            }

            o.put("on", true);
            o.put("app", app);
            o.put("title", title == null ? "" : title);
            o.put("artist", artist == null ? "" : artist);
            o.put("album", album == null ? "" : album);
            o.put("playing", playing);
            o.put("pos", Math.max(0, pos));
            o.put("dur", Math.max(0, dur));
            o.put("vol", vol);
            o.put("volMax", volMax);
            o.put("art", artSmall != null ? artKey : 0);
            if (accent != null) o.put("accent", rgb(accent));
            if (dark != null) o.put("dark", rgb(dark));
            link.sendMedia(o, artSmall != null ? artKey : 0, artSmall);
        } catch (Exception ignored) {}
    }

    private static double lum(int c) {
        return (0.2126 * Color.red(c) + 0.7152 * Color.green(c) + 0.0722 * Color.blue(c)) / 255.0;
    }

    private static int mix(int c, int with, double t) {
        return Color.rgb(
            (int) Math.round(Color.red(c) * (1 - t) + Color.red(with) * t),
            (int) Math.round(Color.green(c) * (1 - t) + Color.green(with) * t),
            (int) Math.round(Color.blue(c) * (1 - t) + Color.blue(with) * t)
        );
    }

    /** Kapaktan iki renk: vurgu (canlı) ve koyu kart rengi. */
    private void colors(Bitmap art) {
        try {
            Palette p = Palette.from(art).maximumColorCount(16).generate();
            int fallback = p.getDominantColor(Color.rgb(207, 68, 40));
            int a = p.getVibrantColor(p.getLightVibrantColor(p.getDarkVibrantColor(fallback)));
            // Açık kâğıt zeminde okunsun diye çok açık renkler koyulaştırılır.
            while (lum(a) > 0.62) a = mix(a, Color.BLACK, 0.15);
            if (lum(a) < 0.12) a = mix(a, Color.WHITE, 0.35);
            int d = p.getDarkMutedColor(p.getDarkVibrantColor(mix(a, Color.BLACK, 0.7)));
            while (lum(d) > 0.18) d = mix(d, Color.BLACK, 0.2);
            accent = new int[] { Color.red(a), Color.green(a), Color.blue(a) };
            dark = new int[] { Color.red(d), Color.green(d), Color.blue(d) };
        } catch (Exception e) {
            accent = dark = null;
        }
    }

    /** Masa saatinden gelen komut. */
    void command(String cmd) {
        h.post(() -> {
            try {
                AudioManager am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
                boolean remote = ctrl != null && ctrl.getPlaybackInfo() != null &&
                    ctrl.getPlaybackInfo().getPlaybackType() == MediaController.PlaybackInfo.PLAYBACK_TYPE_REMOTE;
                switch (cmd) {
                    case "toggle": {
                        if (ctrl == null) return;
                        PlaybackState st = ctrl.getPlaybackState();
                        if (st != null && st.getState() == PlaybackState.STATE_PLAYING) ctrl.getTransportControls().pause();
                        else ctrl.getTransportControls().play();
                        break;
                    }
                    case "next":
                        if (ctrl != null) ctrl.getTransportControls().skipToNext();
                        break;
                    case "prev":
                        if (ctrl != null) ctrl.getTransportControls().skipToPrevious();
                        break;
                    case "volup":
                        if (remote) ctrl.adjustVolume(AudioManager.ADJUST_RAISE, 0);
                        else am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, 0);
                        break;
                    case "voldown":
                        if (remote) ctrl.adjustVolume(AudioManager.ADJUST_LOWER, 0);
                        else am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, 0);
                        break;
                    default:
                        return;
                }
            } catch (Exception ignored) {}
            schedule();
        });
    }
}
