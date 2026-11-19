package com.loljpg.masasaati;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Uygulama kapalıyken masa saati bağlantısını açık tutan ön plan servisi.
 * Bildirim çubuğunda küçük, sessiz bir "Masa saatine bağlı" satırı gösterir.
 */
public class BleService extends Service {

    private static final String CHANNEL = "masa_saati";
    private static final int ID = 7101;

    public static void start(Context c) {
        Intent i = new Intent(c, BleService.class);
        try {
            if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i);
            else c.startService(i);
        } catch (Exception e) {
            // Arka plandan başlatılamıyorsa bağlantı yine de süreç yaşadıkça sürer.
            BleLink.get(c).start();
        }
    }

    public static void stop(Context c) {
        c.stopService(new Intent(c, BleService.class));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && nm != null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Masa saati bağlantısı", NotificationManager.IMPORTANCE_MIN);
            ch.setShowBadge(false);
            nm.createNotificationChannel(ch);
        }
        try {
            if (Build.VERSION.SDK_INT >= 34) startForeground(ID, build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            else startForeground(ID, build());
        } catch (Exception e) {
            // İzin yoksa (Bluetooth izni verilmemiş) servis duramaz; bağlantı süreç içinde sürer.
            BleLink.get(this).start();
            stopSelf();
            return;
        }
        BleLink link = BleLink.get(this);
        link.setServiceNotifier(() -> {
            NotificationManager m = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (m != null) m.notify(ID, build());
        });
        link.start();
    }

    private Notification build() {
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent pi = launch == null ? null : PendingIntent.getActivity(this, 0, launch, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        b.setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Çalışma Asistanı")
            .setContentText(BleLink.get(this).statusLine())
            .setOngoing(true)
            .setShowWhen(false);
        if (pi != null) b.setContentIntent(pi);
        return b.build();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        BleLink.get(this).start();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        BleLink.get(this).setServiceNotifier(null);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
