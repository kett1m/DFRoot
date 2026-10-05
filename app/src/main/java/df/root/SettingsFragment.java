package df.root;

import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.preference.Preference;
import androidx.preference.PreferenceDataStore;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.SwitchPreferenceCompat;

public class SettingsFragment extends PreferenceFragmentCompat {

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        Context ctx = requireContext();
        SharedPreferences dePrefs = ctx.createDeviceProtectedStorageContext()
                .getSharedPreferences(ExploitRunner.PREFS_NAME, Context.MODE_PRIVATE);
        getPreferenceManager().setPreferenceDataStore(new DePreferenceDataStore(dePrefs));
        setPreferencesFromResource(R.xml.preferences, rootKey);

        Preference cleanerPref = findPreference("root_cleaner");
        cleanerPref.setOnPreferenceClickListener(pref -> {
            new AlertDialog.Builder(requireContext())
                    .setTitle(R.string.root_cleaner_title)
                    .setMessage(R.string.root_cleaner_message)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(R.string.root_cleaner_open_settings,
                            (dialog, which) -> openAppStorageSettings())
                    .show();
            return true;
        });

        ComponentName bootReceiver = new ComponentName(ctx, BootReceiver.class);
        SwitchPreferenceCompat bootPref = findPreference("boot_start");
        int state = ctx.getPackageManager().getComponentEnabledSetting(bootReceiver);
        bootPref.setChecked(state == PackageManager.COMPONENT_ENABLED_STATE_ENABLED);
        bootPref.setOnPreferenceChangeListener((pref, value) -> {
            ctx.getPackageManager().setComponentEnabledSetting(bootReceiver,
                    (Boolean) value ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                                    : PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                    PackageManager.DONT_KILL_APP);
            return true;
        });
    }

    private void openAppStorageSettings() {
        Context context = requireContext();
        Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.fromParts("package", context.getPackageName(), null));
        try {
            startActivity(intent);
        } catch (ActivityNotFoundException | SecurityException e) {
            Toast.makeText(context, R.string.root_cleaner_unavailable,
                    Toast.LENGTH_LONG).show();
        }
    }

    private static class DePreferenceDataStore extends PreferenceDataStore {
        private final SharedPreferences mPrefs;

        DePreferenceDataStore(SharedPreferences prefs) {
            mPrefs = prefs;
        }

        @Override
        public void putBoolean(String key, boolean value) {
            mPrefs.edit().putBoolean(key, value).apply();
        }

        @Override
        public boolean getBoolean(String key, boolean defValue) {
            return mPrefs.getBoolean(key, defValue);
        }
    }
}
