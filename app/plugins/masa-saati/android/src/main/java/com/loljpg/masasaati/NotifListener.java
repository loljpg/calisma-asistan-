package com.loljpg.masasaati;

import android.app.Notification;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.provider.Settings;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.json.JSONObject;

/**
 * Telefona gelen bildirimleri masa saatine iletir ve müzik oturumlarını izler.
 * Kullanıcı "Bildirim erişimi" izni verince sistem bu servisi açık tutar.
 */
public class NotifListener extends NotificationListenerService {

    static NotifListener current;
    private final Map<String, String> lastSent = new HashMap<>();
    private final Map<String, Long> lastAt = new HashMap<>();

    public static boolean isEnabled(Context c) {
        String flat = Settings.Secure.getString(c.getContentResolver(), "enabled_notification_listeners");
        if (flat == null) return false;
        String me = new ComponentName(c, NotifListener.class).flattenToString();
        for (String s : flat.split(":")) if (s.equals(me)) return true;
        return false;
    }

    @Override
    public void onListenerConnected() {
        current = this;
        BleLink.get(this).start();
        MediaBridge.get(this).attach();
    }

    @Override
    public void onListenerDisconnected() {
        if (current == this) current = null;
        MediaBridge.get(this).detach();
    }

    @Override
    public void onNotificationPosted(StatusBarNotification sbn) {
        try {
            forward(sbn);
        } catch (Exception ignored) {}
    }

    @Override
    public void onNotificationRemoved(StatusBarNotification sbn) {
        if (sbn == null) return;
        if (lastSent.remove(sbn.getKey()) != null) BleLink.get(this).removeNotification(id(sbn));
    }

    private static String id(StatusBarNotification sbn) {
        return Integer.toHexString(sbn.getKey().hashCode());
    }

    private void forward(StatusBarNotification sbn) throws Exception {
        BleLink link = BleLink.get(this);
        if (!link.isAuthed() || !link.prefs.getBoolean("notif", true)) return;
        String pkg = sbn.getPackageName();
        if (pkg.equals(getPackageName()) || pkg.equals("android") || pkg.equals("com.android.systemui")) return;
        Set<String> blocked = link.prefs.getStringSet("blocked", new HashSet<>());
        if (blocked.contains(pkg)) return;
        Notification n = sbn.getNotification();
        if (n == null) return;
        if ((n.flags & Notification.FLAG_ONGOING_EVENT) != 0 || (n.flags & Notification.FLAG_GROUP_SUMMARY) != 0) return;
        if (Notification.CATEGORY_TRANSPORT.equals(n.category) || Notification.CATEGORY_PROGRESS.equals(n.category)) return;
        Bundle ex = n.extras;
        CharSequence title = ex.getCharSequence(Notification.EXTRA_TITLE);
        CharSequence text = ex.getCharSequence(Notification.EXTRA_BIG_TEXT);
        if (text == null) text = ex.getCharSequence(Notification.EXTRA_TEXT);
        String t = title == null ? "" : title.toString().trim();
        String x = text == null ? "" : text.toString().trim().replace('\n', ' ');
        if (t.isEmpty() && x.isEmpty()) return;
        if (x.length() > 160) x = x.substring(0, 160);
        if (t.length() > 80) t = t.substring(0, 80);

        // Aynı bildirim aynı metinle tekrar gelirse (ör. sessiz güncelleme) gönderilmez.
        String key = sbn.getKey(), sig = t + "\u0000" + x;
        Long prev = lastAt.get(key);
        if (sig.equals(lastSent.get(key)) && prev != null && System.currentTimeMillis() - prev < 60000) return;
        lastSent.put(key, sig);
        lastAt.put(key, System.currentTimeMillis());

        PackageManager pm = getPackageManager();
        String app = pkg;
        Bitmap icon = null;
        try {
            ApplicationInfo ai = pm.getApplicationInfo(pkg, 0);
            app = pm.getApplicationLabel(ai).toString();
            Drawable d = pm.getApplicationIcon(ai);
            icon = Frames.drawable(d, 20, 20);
        } catch (Exception ignored) {}
        long iconId = pkg.hashCode() & 0xFFFFFFFFL;

        JSONObject o = new JSONObject();
        o.put("id", id(sbn));
        o.put("app", app);
        o.put("pkg", pkg);
        o.put("title", t);
        o.put("text", x);
        o.put("ic", iconId);
        o.put("t", sbn.getPostTime());
        link.sendNotification(o, iconId, icon);
    }
}
