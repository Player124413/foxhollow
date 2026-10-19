package dev.foxhollow;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

final class Settings {
    static final String[] SCALES = {"0.75", "1", "1.5", "2"};
    static final String[] ASPECTS = {"narrow", "wide"};
    private final SharedPreferences preferences;

    Settings(Context context) {
        preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE);
    }

    int index(String key, int count, int fallback) {
        try {
            int value = preferences.getInt(key, fallback);
            return value >= 0 && value < count ? value : fallback;
        } catch (ClassCastException error) {
            return fallback;
        }
    }

    boolean flag(String key, boolean fallback) {
        try { return preferences.getBoolean(key, fallback); }
        catch (ClassCastException error) { return fallback; }
    }

    void put(String key, int value) { preferences.edit().putInt(key, value).apply(); }
    void put(String key, boolean value) { preferences.edit().putBoolean(key, value).apply(); }
    void reset() { preferences.edit().clear().apply(); }

    Intent gameIntent(Context context) {
        return new Intent(context, GameActivity.class)
            .putExtra("renderer", "opengles")
            .putExtra("scale", SCALES[index("scale", SCALES.length, 1)])
            .putExtra("aspect", ASPECTS[index("aspect", ASPECTS.length, 0)])
            .putExtra("volume", index("volume", 101, 100))
            .putExtra("vsync", flag("vsync", true))
            .putExtra("etc2", flag("etc2", true))
            .putExtra("fps", flag("fps", false))
            .putExtra("touch", flag("touch", true))
            .putExtra("editor", flag("editor", false))
            .putExtra("keepScreenOn", flag("keepScreenOn", true));
    }
}
