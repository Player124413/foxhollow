package dev.foxhollow;

import android.content.Context;
import android.content.Intent;
import android.widget.SeekBar;
import android.widget.Spinner;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;
import java.io.File;

@RunWith(AndroidJUnit4.class)
public final class LauncherTest {
    private Context context;
    private android.content.SharedPreferences preferences;
    private java.util.Map<String, ?> original;

    @Before public void setup() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE);
        original = new java.util.HashMap<>(preferences.getAll());
        preferences.edit().clear().commit();
    }

    @After public void restore() {
        android.content.SharedPreferences.Editor editor = preferences.edit().clear();
        for (java.util.Map.Entry<String, ?> entry : original.entrySet()) {
            Object value = entry.getValue();
            if (value instanceof Integer) editor.putInt(entry.getKey(), (Integer)value);
            else if (value instanceof Boolean) editor.putBoolean(entry.getKey(), (Boolean)value);
            else if (value instanceof String) editor.putString(entry.getKey(), (String)value);
        }
        editor.commit();
    }

    @Test public void defaultsReachGameIntent() {
        Intent intent = new Settings(context).gameIntent(context);
        assertEquals("opengles", intent.getStringExtra("renderer"));
        assertEquals("1", intent.getStringExtra("scale"));
        assertEquals("narrow", intent.getStringExtra("aspect"));
        assertEquals(100, intent.getIntExtra("volume", -1));
        assertTrue(intent.getBooleanExtra("touch", false));
        assertTrue(intent.getBooleanExtra("vsync", false));
        assertTrue(intent.getBooleanExtra("etc2", false));
        assertFalse(intent.getBooleanExtra("editor", true));
    }

    @Test public void oldRendererSettingsUseOpenGles() {
        Settings settings = new Settings(context);
        for (int renderer : new int[] {0, 1, 2, -1, 100}) {
            settings.put("renderer", renderer);
            assertEquals("opengles", settings.gameIntent(context).getStringExtra("renderer"));
        }
    }

    @Test public void settingsPersist() {
        Settings settings = new Settings(context);
        settings.put("renderer", 2);
        settings.put("scale", 0);
        settings.put("volume", 25);
        settings.put("touch", false);
        settings.put("etc2", false);
        Intent intent = new Settings(context).gameIntent(context);
        assertEquals("opengles", intent.getStringExtra("renderer"));
        assertEquals("0.75", intent.getStringExtra("scale"));
        assertEquals(25, intent.getIntExtra("volume", -1));
        assertFalse(intent.getBooleanExtra("touch", true));
        assertFalse(intent.getBooleanExtra("etc2", true));
    }

    @Test public void corruptedSettingsFallBack() {
        preferences.edit().putString("renderer", "bad").putInt("scale", -20)
            .putInt("volume", 10000).putString("touch", "bad").commit();
        Intent intent = new Settings(context).gameIntent(context);
        assertEquals("opengles", intent.getStringExtra("renderer"));
        assertEquals("1", intent.getStringExtra("scale"));
        assertEquals(100, intent.getIntExtra("volume", -1));
        assertTrue(intent.getBooleanExtra("touch", false));
    }

    @Test public void localizedLauncherResources() {
        for (String language : new String[] {"en", "ru"}) {
            android.content.res.Configuration config = new android.content.res.Configuration(context.getResources().getConfiguration());
            config.setLocale(new java.util.Locale(language));
            Context localized = context.createConfigurationContext(config);
            assertEquals(language.equals("ru") ? "Проверить обновления" : "Check for updates",
                localized.getString(R.string.check_updates));
            String message = localized.getString(R.string.community_message);
            assertTrue(message.contains("https://t.me/player1444ports"));
            assertTrue(message.contains("https://github.com/JackPriceBurns/foxhollow"));
            assertTrue(message.contains(language.equals("ru") ? "порт бы не вышел" : "would not have been possible"));
        }
    }

    @Test public void launcherBindsSettings() {
        try (ActivityScenario<LauncherActivity> scenario = ActivityScenario.launch(LauncherActivity.class)) {
            scenario.onActivity(activity -> {
                assertEquals(new File(context.getFilesDir(), "disc.img").isFile(), activity.findViewById(R.id.play).isEnabled());
                assertEquals(1, ((Spinner)activity.findViewById(R.id.render_scale)).getSelectedItemPosition());
                assertEquals(100, ((SeekBar)activity.findViewById(R.id.volume)).getProgress());
                assertNotNull(activity.findViewById(R.id.touch_editor));
            });
        }
    }
}
