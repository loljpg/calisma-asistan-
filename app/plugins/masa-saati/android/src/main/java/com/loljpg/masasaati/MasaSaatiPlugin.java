package com.loljpg.masasaati;

import android.Manifest;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.provider.Settings;
import com.getcapacitor.JSArray;
import com.getcapacitor.JSObject;
import com.getcapacitor.PermissionState;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.getcapacitor.annotation.Permission;
import com.getcapacitor.annotation.PermissionCallback;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONObject;

/** Uygulamanın JS tarafına açılan köprü: registerPlugin('MasaSaati'). */
@CapacitorPlugin(
    name = "MasaSaati",
    permissions = {
        @Permission(alias = "bluetooth", strings = { Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT }),
        @Permission(alias = "location", strings = { Manifest.permission.ACCESS_FINE_LOCATION })
    }
)
public class MasaSaatiPlugin extends Plugin {

    private BleLink link;

    @Override
    public void load() {
        link = BleLink.get(getContext());
        link.setListener(
            new BleLink.Listener() {
                @Override
                public void onState() {
                    notifyListeners("state", toJS(link.state()));
                }

                @Override
                public void onPending() {
                    notifyListeners("pending", new JSObject());
                }
            }
        );
        if (link.isPaired() && link.hasPermission()) BleService.start(getContext());
    }

    private static JSObject toJS(JSONObject o) {
        try {
            return JSObject.fromJSONObject(o);
        } catch (Exception e) {
            return new JSObject();
        }
    }

    private String alias() {
        return Build.VERSION.SDK_INT >= 31 ? "bluetooth" : "location";
    }

    @PluginMethod
    public void getState(PluginCall call) {
        call.resolve(toJS(link.state()));
    }

    @PluginMethod
    public void requestBluetooth(PluginCall call) {
        if (getPermissionState(alias()) == PermissionState.GRANTED) {
            JSObject r = new JSObject();
            r.put("granted", true);
            call.resolve(r);
            return;
        }
        requestPermissionForAlias(alias(), call, "bluetoothPermsCallback");
    }

    @PermissionCallback
    private void bluetoothPermsCallback(PluginCall call) {
        JSObject r = new JSObject();
        boolean granted = getPermissionState(alias()) == PermissionState.GRANTED;
        r.put("granted", granted);
        if (granted && link.isPaired()) BleService.start(getContext());
        call.resolve(r);
    }

    @PluginMethod
    public void scan(PluginCall call) {
        if (!link.hasPermission()) {
            call.reject("Bluetooth izni yok");
            return;
        }
        link.scan(devices -> {
            JSObject r = new JSObject();
            JSArray arr = new JSArray();
            try {
                for (Object o : toList(devices)) arr.put(o);
            } catch (Exception ignored) {}
            r.put("devices", arr);
            call.resolve(r);
        });
    }

    private static List<Object> toList(JSONArray a) throws Exception {
        List<Object> out = new ArrayList<>();
        for (int i = 0; i < a.length(); i++) out.add(toJS(a.getJSONObject(i)));
        return out;
    }

    @PluginMethod
    public void pair(PluginCall call) {
        String address = call.getString("address");
        if (address == null) {
            call.reject("Adres yok");
            return;
        }
        link.pair(address, call.getString("name", "Çalışma Saati"), (ok, msg) -> {
            if (ok) {
                BleService.start(getContext());
                call.resolve(toJS(link.state()));
            } else {
                call.reject(msg);
            }
        });
    }

    @PluginMethod
    public void unpair(PluginCall call) {
        link.unpair();
        BleService.stop(getContext());
        call.resolve();
    }

    @PluginMethod
    public void setView(PluginCall call) {
        JSObject data = call.getObject("data");
        if (data != null) link.setView(data.toString());
        call.resolve();
    }

    @PluginMethod
    public void setTimer(PluginCall call) {
        JSObject data = call.getObject("data");
        if (data != null) link.setTimer(data.toString());
        call.resolve();
    }

    @PluginMethod
    public void takePending(PluginCall call) {
        call.resolve(toJS(link.takePending()));
    }

    @PluginMethod
    public void setOptions(PluginCall call) {
        Set<String> blocked = null;
        JSArray b = call.getArray("blocked");
        if (b != null) {
            blocked = new HashSet<>();
            try {
                for (Object o : b.<Object>toList()) blocked.add(String.valueOf(o));
            } catch (Exception ignored) {}
        }
        link.setOptions(call.getBoolean("notif"), call.getBoolean("media"), blocked);
        call.resolve(toJS(link.state()));
    }

    @PluginMethod
    public void listApps(PluginCall call) {
        PackageManager pm = getContext().getPackageManager();
        Intent i = new Intent(Intent.ACTION_MAIN);
        i.addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> list = pm.queryIntentActivities(i, 0);
        Set<String> seen = new HashSet<>();
        List<JSObject> apps = new ArrayList<>();
        for (ResolveInfo ri : list) {
            String pkg = ri.activityInfo.packageName;
            if (pkg.equals(getContext().getPackageName()) || !seen.add(pkg)) continue;
            JSObject o = new JSObject();
            o.put("pkg", pkg);
            o.put("name", ri.loadLabel(pm).toString());
            o.put("system", (ri.activityInfo.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0);
            apps.add(o);
        }
        apps.sort((a, b) -> a.getString("name", "").compareToIgnoreCase(b.getString("name", "")));
        JSArray arr = new JSArray();
        for (JSObject o : apps) arr.put(o);
        JSObject r = new JSObject();
        r.put("apps", arr);
        call.resolve(r);
    }

    @PluginMethod
    public void openNotificationAccess(PluginCall call) {
        try {
            Intent i = new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(i);
            call.resolve();
        } catch (Exception e) {
            call.reject("Ayarlar açılamadı");
        }
    }

    @PluginMethod
    public void openBluetoothSettings(PluginCall call) {
        try {
            Intent i = new Intent(Settings.ACTION_BLUETOOTH_SETTINGS);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(i);
            call.resolve();
        } catch (Exception e) {
            call.reject("Ayarlar açılamadı");
        }
    }
}
