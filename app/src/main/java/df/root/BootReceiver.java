package df.root;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.PowerManager;
import android.util.Log;

import java.io.File;

public class BootReceiver extends BroadcastReceiver implements IReporter {
    private static final String TAG = "dfroot";

    @Override
    public void report(String msg) {
        Log.i(TAG, msg.trim());
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        File df = new File("/dev/df");
        if (df.exists()) {
            File success = new File("/dev/dfm4");
            if (success.exists()) {
                Log.i(TAG, "boot: already hooked, skipping");
                return;
            }
            /* Stale mutex from a previous failed attempt — clean up */
            Log.i(TAG, "boot: stale mutex detected, cleaning up");
            df.delete();
            for (int i = 0; i <= 6; i++)
                new File("/dev/dfm" + i).delete();
        }
        Log.i(TAG, "boot: " + intent.getAction());
        final Context deCtx = context.createDeviceProtectedStorageContext();
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        PowerManager.WakeLock wl = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "dfroot:boot");
        wl.acquire();
        new Thread(() -> {
            try {
                int rc = ExploitRunner.run(deCtx, this);
                Log.i(TAG, "boot: exploit rc=" + rc);
            } catch (Exception e) {
                Log.e(TAG, "boot: exploit exception", e);
            } finally {
                wl.release();
            }
        }, "dfroot-boot").start();
    }
}
