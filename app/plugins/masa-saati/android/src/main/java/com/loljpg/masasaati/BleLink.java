package com.loljpg.masasaati;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import java.security.SecureRandom;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Masa saatiyle Bluetooth (BLE) bağlantısı. Uygulama kapalıyken de BleService ya da
 * NotifListener içinde yaşar. Tüm durum ana iş parçacığında (handler) değişir.
 *
 * Telefon merkez (central), masa saati çevre birimi (peripheral). Eşleşme: telefon rastgele bir
 * anahtar üretir; masa saati ilk bağlanan ya da eşleşme kipindeyken bağlanan telefonun anahtarını saklar.
 */
@SuppressLint("MissingPermission")
public final class BleLink {

    public static final UUID SERVICE = UUID.fromString("6e3a0001-5a17-4c2d-9b8e-0c41a5a7c0de");
    public static final UUID RX = UUID.fromString("6e3a0002-5a17-4c2d-9b8e-0c41a5a7c0de"); // telefon yazar
    public static final UUID TX = UUID.fromString("6e3a0003-5a17-4c2d-9b8e-0c41a5a7c0de"); // masa saati bildirir
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    public interface Listener {
        void onState();

        void onPending();
    }

    public interface Result {
        void done(boolean ok, String message);
    }

    private static BleLink inst;

    public static synchronized BleLink get(Context c) {
        if (inst == null) inst = new BleLink(c.getApplicationContext());
        return inst;
    }

    final Context ctx;
    final SharedPreferences prefs;
    final Handler h = new Handler(Looper.getMainLooper());

    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rxChar;
    private boolean started, connecting, connected, authed, scanning, receiverOn;
    private int mtu = 23;
    private final ArrayDeque<byte[]> outq = new ArrayDeque<>();
    private boolean writing;
    private long writeAt;
    private final Frames.Reader reader = new Frames.Reader();
    private Listener listener;
    private Runnable serviceNotifier;
    private String deviceStatus = "{}";
    private String lastError = "";

    // Eşleşme isteği
    private String pairAddress, pairToken;
    private Result pairResult;

    // Bu bağlantıda gönderilen simgeler ve kapak
    private final Set<Long> sentIcons = new HashSet<>();
    private long sentArt = 0;

    private BleLink(Context c) {
        ctx = c;
        prefs = c.getSharedPreferences("masa_saati", Context.MODE_PRIVATE);
    }

    // ---------------------------------------------------------------- Durum

    public void setListener(Listener l) {
        listener = l;
    }

    void setServiceNotifier(Runnable r) {
        serviceNotifier = r;
    }

    public boolean isPaired() {
        return prefs.getString("addr", null) != null && prefs.getString("token", null) != null;
    }

    public boolean isAuthed() {
        return authed;
    }

    BluetoothAdapter adapter() {
        BluetoothManager m = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        return m == null ? null : m.getAdapter();
    }

    boolean hasPermission() {
        if (Build.VERSION.SDK_INT >= 31) {
            return ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
                ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED;
        }
        return ctx.checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    public JSONObject state() {
        JSONObject o = new JSONObject();
        try {
            BluetoothAdapter a = adapter();
            o.put("supported", a != null);
            o.put("enabled", a != null && a.isEnabled());
            o.put("permission", hasPermission());
            o.put("paired", isPaired());
            o.put("address", prefs.getString("addr", ""));
            o.put("name", prefs.getString("name", ""));
            o.put("connected", connected);
            o.put("authed", authed);
            o.put("scanning", scanning);
            o.put("error", lastError);
            o.put("device", new JSONObject(deviceStatus));
            o.put("notif", prefs.getBoolean("notif", true));
            o.put("media", prefs.getBoolean("media", true));
            o.put("notifAccess", NotifListener.isEnabled(ctx));
            JSONArray b = new JSONArray();
            for (String s : prefs.getStringSet("blocked", new HashSet<>())) b.put(s);
            o.put("blocked", b);
        } catch (Exception ignored) {}
        return o;
    }

    String statusLine() {
        if (authed) return "Masa saatine bağlı";
        if (connecting || connected) return "Masa saatine bağlanıyor…";
        return "Masa saati bekleniyor";
    }

    private void changed() {
        if (listener != null) listener.onState();
        if (serviceNotifier != null) serviceNotifier.run();
    }

    // ---------------------------------------------------------------- Başlat / durdur

    /** Eşleşmiş bir masa saati varsa bağlanmaya başlar (birden çok kez çağrılabilir). */
    public void start() {
        h.post(() -> {
            if (!receiverOn) {
                IntentFilter f = new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED);
                if (Build.VERSION.SDK_INT >= 33) ctx.registerReceiver(btReceiver, f, Context.RECEIVER_EXPORTED);
                else ctx.registerReceiver(btReceiver, f);
                receiverOn = true;
                h.postDelayed(clockTick, 10 * 60 * 1000);
            }
            started = true;
            if (!isPaired() || gatt != null || connecting) return;
            connect();
        });
    }

    public void stop() {
        h.post(() -> {
            started = false;
            stopScan();
            closeGatt();
            changed();
        });
    }

    private final BroadcastReceiver btReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
            int s = i.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR);
            if (s == BluetoothAdapter.STATE_ON && started) {
                h.postDelayed(BleLink.this::connect, 1000);
            } else if (s == BluetoothAdapter.STATE_TURNING_OFF || s == BluetoothAdapter.STATE_OFF) {
                stopScan();
                closeGatt();
            }
            changed();
        }
    };

    private final Runnable clockTick = new Runnable() {
        @Override
        public void run() {
            if (authed) sendClock();
            h.postDelayed(this, 10 * 60 * 1000);
        }
    };

    // ---------------------------------------------------------------- Bağlantı

    private void connect() {
        String addr = pairAddress != null ? pairAddress : prefs.getString("addr", null);
        BluetoothAdapter a = adapter();
        if (addr == null || a == null || !a.isEnabled() || !hasPermission()) return;
        if (gatt != null || connecting) return;
        stopScan();
        try {
            BluetoothDevice d = a.getRemoteDevice(addr);
            connecting = true;
            mtu = 23;
            reader.reset();
            gatt = d.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE);
            h.removeCallbacks(connectTimeout);
            h.postDelayed(connectTimeout, 15000);
        } catch (Exception e) {
            connecting = false;
            lastError = "Bağlanılamadı";
            scheduleReconnect();
        }
        changed();
    }

    private final Runnable connectTimeout = () -> {
        if (!authed) {
            lastError = "Masa saati bulunamadı";
            closeGatt();
            failPair("Masa saati bulunamadı. Yakında ve açık mı?");
            scheduleReconnect();
            changed();
        }
    };

    private void closeGatt() {
        h.removeCallbacks(connectTimeout);
        if (gatt != null) {
            try {
                gatt.disconnect();
            } catch (Exception ignored) {}
            try {
                gatt.close();
            } catch (Exception ignored) {}
        }
        gatt = null;
        rxChar = null;
        connecting = connected = authed = false;
        outq.clear();
        writing = false;
    }

    /** Bağlantı koptuysa masa saati yeniden görünene kadar düşük güçte tarar. */
    private void scheduleReconnect() {
        if (!started || !isPaired() || pairAddress != null) return;
        h.postDelayed(this::startScan, 3000);
    }

    private final ScanCallback scanCb = new ScanCallback() {
        @Override
        public void onScanResult(int type, ScanResult r) {
            h.post(() -> {
                if (!scanning) return;
                stopScan();
                connect();
            });
        }

        @Override
        public void onScanFailed(int code) {
            h.post(() -> {
                scanning = false;
                h.postDelayed(BleLink.this::startScan, 30000);
            });
        }
    };

    private void startScan() {
        if (!started || scanning || gatt != null || connecting) return;
        BluetoothAdapter a = adapter();
        String addr = prefs.getString("addr", null);
        if (a == null || !a.isEnabled() || addr == null || !hasPermission()) return;
        BluetoothLeScanner s = a.getBluetoothLeScanner();
        if (s == null) return;
        try {
            List<ScanFilter> filters = new ArrayList<>();
            filters.add(new ScanFilter.Builder().setDeviceAddress(addr).build());
            ScanSettings st = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_POWER).build();
            s.startScan(filters, st, scanCb);
            scanning = true;
        } catch (Exception e) {
            scanning = false;
            h.postDelayed(this::startScan, 30000);
        }
        changed();
    }

    private void stopScan() {
        if (!scanning) return;
        scanning = false;
        try {
            BluetoothAdapter a = adapter();
            if (a != null && a.getBluetoothLeScanner() != null) a.getBluetoothLeScanner().stopScan(scanCb);
        } catch (Exception ignored) {}
    }

    // ---------------------------------------------------------------- Eşleşme

    public interface FoundListener {
        void done(JSONArray devices);
    }

    /** Masa saatlerini 6 saniye arar (yalnızca uygulama açıkken). */
    public void scan(FoundListener done) {
        h.post(() -> {
            BluetoothAdapter a = adapter();
            if (a == null || !a.isEnabled() || a.getBluetoothLeScanner() == null) {
                done.done(new JSONArray());
                return;
            }
            Map<String, JSONObject> found = new HashMap<>();
            ScanCallback c = new ScanCallback() {
                @Override
                public void onScanResult(int type, ScanResult r) {
                    try {
                        JSONObject o = new JSONObject();
                        String name = r.getScanRecord() != null ? r.getScanRecord().getDeviceName() : null;
                        if (name == null) name = r.getDevice().getName();
                        o.put("address", r.getDevice().getAddress());
                        o.put("name", name == null ? "Çalışma Saati" : name);
                        o.put("rssi", r.getRssi());
                        synchronized (found) {
                            found.put(r.getDevice().getAddress(), o);
                        }
                    } catch (Exception ignored) {}
                }
            };
            boolean wasScanning = scanning;
            stopScan();
            try {
                List<ScanFilter> filters = new ArrayList<>();
                filters.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build());
                ScanSettings st = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
                a.getBluetoothLeScanner().startScan(filters, st, c);
            } catch (Exception e) {
                done.done(new JSONArray());
                return;
            }
            h.postDelayed(() -> {
                try {
                    a.getBluetoothLeScanner().stopScan(c);
                } catch (Exception ignored) {}
                JSONArray arr = new JSONArray();
                synchronized (found) {
                    for (JSONObject o : found.values()) arr.put(o);
                }
                done.done(arr);
                if (wasScanning) startScan();
            }, 6000);
        });
    }

    public void pair(String address, String name, Result result) {
        h.post(() -> {
            stopScan();
            closeGatt();
            byte[] r = new byte[16];
            new SecureRandom().nextBytes(r);
            StringBuilder sb = new StringBuilder();
            for (byte b : r) sb.append(String.format("%02x", b));
            pairAddress = address;
            pairToken = sb.toString();
            pairResult = result;
            prefs.edit().putString("pairName", name).apply();
            started = true;
            connect();
        });
    }

    private void failPair(String msg) {
        if (pairResult == null) return;
        Result r = pairResult;
        pairResult = null;
        pairAddress = null;
        pairToken = null;
        r.done(false, msg);
    }

    public void unpair() {
        h.post(() -> {
            if (authed) send('U', Frames.utf8("{}"));
            h.postDelayed(() -> {
                prefs.edit().remove("addr").remove("token").remove("name").apply();
                closeGatt();
                stopScan();
                deviceStatus = "{}";
                changed();
            }, 400);
        });
    }

    // ---------------------------------------------------------------- GATT

    private final BluetoothGattCallback cb = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            h.post(() -> onConn(g, status, newState));
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int m, int status) {
            h.post(() -> {
                if (g != gatt) return;
                if (status == BluetoothGatt.GATT_SUCCESS) mtu = m;
                if (!g.discoverServices()) closeAndRetry("Servis bulunamadı");
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            h.post(() -> onServices(g, status));
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            h.post(() -> {
                if (g == gatt) hello();
            });
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            h.post(() -> {
                if (g != gatt) return;
                writing = false;
                pump();
            });
        }

        @SuppressWarnings("deprecation")
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            byte[] v = c.getValue();
            if (v == null) return;
            byte[] copy = v.clone();
            h.post(() -> {
                if (g == gatt) onBytes(copy);
            });
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            byte[] copy = value.clone();
            h.post(() -> {
                if (g == gatt) onBytes(copy);
            });
        }
    };

    private void onConn(BluetoothGatt g, int status, int newState) {
        if (g != gatt) {
            try {
                g.close();
            } catch (Exception ignored) {}
            return;
        }
        if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
            connected = true;
            connecting = false;
            g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
            if (!g.requestMtu(247) && !g.discoverServices()) closeAndRetry("Servis bulunamadı");
            changed();
        } else {
            boolean wasPairing = pairResult != null;
            closeGatt();
            lastError = status == BluetoothGatt.GATT_SUCCESS ? "" : "Bağlantı koptu (" + status + ")";
            if (wasPairing) failPair("Masa saatine bağlanılamadı (" + status + "). Tekrar dene.");
            scheduleReconnect();
            changed();
        }
    }

    private void closeAndRetry(String msg) {
        lastError = msg;
        closeGatt();
        failPair(msg);
        scheduleReconnect();
        changed();
    }

    @SuppressWarnings("deprecation")
    private void onServices(BluetoothGatt g, int status) {
        if (g != gatt) return;
        BluetoothGattService s = status == BluetoothGatt.GATT_SUCCESS ? g.getService(SERVICE) : null;
        BluetoothGattCharacteristic rx = s == null ? null : s.getCharacteristic(RX);
        BluetoothGattCharacteristic tx = s == null ? null : s.getCharacteristic(TX);
        if (rx == null || tx == null) {
            closeAndRetry("Bu bir Çalışma Saati değil ya da yazılımı eski");
            return;
        }
        rxChar = rx;
        g.setCharacteristicNotification(tx, true);
        BluetoothGattDescriptor d = tx.getDescriptor(CCCD);
        if (d == null) {
            hello();
            return;
        }
        boolean ok;
        if (Build.VERSION.SDK_INT >= 33) {
            ok = g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == 0;
        } else {
            d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            ok = g.writeDescriptor(d);
        }
        if (!ok) h.postDelayed(this::hello, 300);
    }

    private void hello() {
        try {
            JSONObject o = new JSONObject();
            o.put("token", pairToken != null ? pairToken : prefs.getString("token", ""));
            o.put("pair", pairToken != null);
            o.put("t", System.currentTimeMillis());
            sendRaw('H', Frames.utf8(o.toString()));
        } catch (Exception ignored) {}
    }

    // ---------------------------------------------------------------- Gönderme

    private void sendRaw(char type, byte[] payload) {
        if (gatt == null || rxChar == null) return;
        byte[] f = Frames.encode(type, payload);
        int chunk = Math.max(20, mtu - 3);
        for (int i = 0; i < f.length; i += chunk) {
            byte[] part = new byte[Math.min(chunk, f.length - i)];
            System.arraycopy(f, i, part, 0, part.length);
            outq.add(part);
        }
        pump();
    }

    /** Yalnızca eşleşme doğrulandıktan sonra gönderir. */
    void send(char type, byte[] payload) {
        if (!authed) return;
        sendRaw(type, payload);
    }

    void sendJson(char type, JSONObject o) {
        send(type, Frames.utf8(o.toString()));
    }

    @SuppressWarnings("deprecation")
    private void pump() {
        if (writing && System.currentTimeMillis() - writeAt > 4000) {
            // Yanıt gelmedi: bağlantı sorunlu, baştan kur.
            closeAndRetry("Masa saati yanıt vermiyor");
            return;
        }
        if (writing || outq.isEmpty() || gatt == null || rxChar == null) return;
        byte[] part = outq.peek();
        boolean ok;
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                ok = gatt.writeCharacteristic(rxChar, part, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0;
            } else {
                rxChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                rxChar.setValue(part);
                ok = gatt.writeCharacteristic(rxChar);
            }
        } catch (Exception e) {
            ok = false;
        }
        if (ok) {
            outq.poll();
            writing = true;
            writeAt = System.currentTimeMillis();
            h.postDelayed(this::pump, 4500);
        } else {
            h.postDelayed(this::pump, 60);
        }
    }

    // ---------------------------------------------------------------- Alma

    private void onBytes(byte[] data) {
        reader.feed(data, this::onFrame);
    }

    private void onFrame(char type, byte[] payload) {
        String s = new String(payload, java.nio.charset.StandardCharsets.UTF_8);
        try {
            switch (type) {
                case 'h': {
                    JSONObject o = new JSONObject(s);
                    if (o.optBoolean("ok")) onAuthed(o);
                    else {
                        lastError = "Masa saati bu telefonu tanımıyor. Masa saatinde Ayarlar → Bluetooth'a dokunup yeniden eşleştir.";
                        failPair("Masa saati eşleşme kipinde değil. Masa saatinde Ayarlar → Bluetooth'a dokun, sonra tekrar dene.");
                        closeGatt();
                        if (started && isPaired()) h.postDelayed(this::startScan, 60000);
                        changed();
                    }
                    break;
                }
                case 'E': {
                    JSONObject ev = new JSONObject(s);
                    int n = ev.length();
                    synchronized (this) {
                        JSONObject pending = new JSONObject(prefs.getString("pendingEvents", "{}"));
                        for (Iterator<String> it = ev.keys(); it.hasNext(); ) {
                            String k = it.next();
                            pending.put(k, ev.get(k));
                        }
                        prefs.edit().putString("pendingEvents", pending.toString()).apply();
                    }
                    JSONObject ack = new JSONObject();
                    ack.put("n", n);
                    sendJson('a', ack);
                    if (listener != null) listener.onPending();
                    break;
                }
                case 'T':
                    synchronized (this) {
                        prefs.edit().putString("pendingTimer", s).apply();
                    }
                    if (listener != null) listener.onPending();
                    break;
                case 'K':
                    MediaBridge.get(ctx).command(new JSONObject(s).optString("cmd"));
                    break;
                case 'S':
                    deviceStatus = s;
                    changed();
                    break;
                default:
                    break;
            }
        } catch (Exception ignored) {}
    }

    private void onAuthed(JSONObject o) {
        h.removeCallbacks(connectTimeout);
        authed = true;
        lastError = "";
        deviceStatus = o.toString();
        if (pairResult != null) {
            String name = prefs.getString("pairName", "Çalışma Saati");
            prefs.edit().putString("addr", pairAddress).putString("token", pairToken).putString("name", name).apply();
            Result r = pairResult;
            pairResult = null;
            pairAddress = null;
            pairToken = null;
            r.done(true, "");
        }
        sentIcons.clear();
        sentArt = 0;
        sendClock();
        String view = prefs.getString("view", null);
        if (view != null) send('V', Frames.utf8(view));
        String timer = prefs.getString("timer", null);
        if (timer != null) send('T', Frames.utf8(timer));
        MediaBridge.get(ctx).resend();
        changed();
    }

    // ---------------------------------------------------------------- Uygulamadan gelenler

    void sendClock() {
        try {
            JSONObject o = new JSONObject();
            o.put("t", System.currentTimeMillis());
            sendJson('C', o);
        } catch (Exception ignored) {}
    }

    public void setView(String json) {
        h.post(() -> {
            prefs.edit().putString("view", json).apply();
            send('V', Frames.utf8(json));
        });
    }

    public void setTimer(String json) {
        h.post(() -> {
            prefs.edit().putString("timer", json).apply();
            send('T', Frames.utf8(json));
        });
    }

    /** Masa saatinden gelip uygulamanın henüz işlemediği olaylar ve kronometre. */
    public JSONObject takePending() {
        JSONObject o = new JSONObject();
        synchronized (this) {
            try {
                o.put("events", new JSONObject(prefs.getString("pendingEvents", "{}")));
                String t = prefs.getString("pendingTimer", null);
                if (t != null) o.put("timer", new JSONObject(t));
            } catch (Exception ignored) {}
            prefs.edit().remove("pendingEvents").remove("pendingTimer").apply();
        }
        return o;
    }

    public void setOptions(Boolean notif, Boolean media, Set<String> blocked) {
        SharedPreferences.Editor e = prefs.edit();
        if (notif != null) e.putBoolean("notif", notif);
        if (media != null) e.putBoolean("media", media);
        if (blocked != null) e.putStringSet("blocked", blocked);
        e.apply();
        h.post(() -> {
            MediaBridge.get(ctx).resend();
            changed();
        });
    }

    // ---------------------------------------------------------------- Bildirim ve müzik

    void sendNotification(JSONObject n, long iconId, Bitmap icon) {
        h.post(() -> {
            if (!authed) return;
            if (icon != null && !sentIcons.contains(iconId)) {
                sentIcons.add(iconId);
                send('I', Frames.image(iconId, icon, true));
            }
            sendJson('N', n);
        });
    }

    void removeNotification(String id) {
        h.post(() -> {
            try {
                JSONObject o = new JSONObject();
                o.put("id", id);
                sendJson('X', o);
            } catch (Exception ignored) {}
        });
    }

    void sendMedia(JSONObject m, long artId, Bitmap art) {
        h.post(() -> {
            if (!authed) return;
            if (art != null && artId != 0 && artId != sentArt) {
                sentArt = artId;
                send('A', Frames.image(artId, art, false));
            }
            sendJson('M', m);
        });
    }
}
