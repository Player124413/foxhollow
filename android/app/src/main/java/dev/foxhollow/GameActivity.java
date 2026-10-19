package dev.foxhollow;

import android.content.Intent;
import android.os.Bundle;
import android.view.WindowManager;
import dev.encounter.borealis.BorealisActivity;
import org.libsdl.app.SDLActivity;
import java.io.File;

public final class GameActivity extends BorealisActivity {
    @Override
    protected void onCreate(Bundle state) {
        Diagnostics.start(this, "game");
        Diagnostics.gameSettings(this, getIntent());
        Diagnostics.gameEvent(this, "phase=activity_create");
        super.onCreate(state);
        Diagnostics.gameEvent(this, "phase=activity_created brokenLibraries=" + mBrokenLibraries);
        if (getIntent().getBooleanExtra("keepScreenOn", true))
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    public void loadLibraries() {
        Diagnostics.gameEvent(this, "phase=load_libraries");
        try {
            super.loadLibraries();
            Diagnostics.gameEvent(this, "phase=libraries_loaded");
        } catch (RuntimeException | LinkageError error) {
            Diagnostics.gameEvent(this, android.util.Log.getStackTraceString(error));
            Diagnostics.gameEvent(this, "phase=load_libraries_failed error=" + error);
            throw error;
        }
    }

    @Override
    protected void main() {
        String library = getMainSharedObject();
        String function = getMainFunction();
        Diagnostics.gameEvent(this, "phase=prepare_arguments");
        String[] arguments = getArguments();
        File disc = new File(getFilesDir(), "disc.img");
        Diagnostics.gameEvent(this, "phase=native_entry library=" + library + " function=" + function
            + " discExists=" + disc.isFile() + " discReadable=" + disc.canRead() + " discBytes=" + disc.length());
        int status = SDLActivity.nativeRunMain(library, function, arguments);
        Diagnostics.gameEvent(this, "phase=native_return status=" + status);
    }

    private static String allowed(String value, String[] choices, String fallback) {
        for (String choice : choices) if (choice.equals(value)) return choice;
        return fallback;
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        SDLActivity.nativeSetenv("FOXHOLLOW_LOG_FILE", new File(Diagnostics.directory(this), "native.log").getAbsolutePath());
        SDLActivity.nativeSetenv("FOXHOLLOW_USER_DIR", getFilesDir().getAbsolutePath());
        SDLActivity.nativeSetenv("FOXHOLLOW_CACHE_DIR", getCacheDir().getAbsolutePath());
        SDLActivity.nativeSetenv("FOXHOLLOW_MEMORY_CARD", new File(getFilesDir(), "memorycard.raw").getAbsolutePath());
        SDLActivity.nativeSetenv("FOXHOLLOW_AUTOSAVE", new File(getFilesDir(), "autosave.bin").getAbsolutePath());
        SDLActivity.nativeSetenv("FOXHOLLOW_FULLSCREEN", "1");
        SDLActivity.nativeSetenv("FOXHOLLOW_RENDERER", "opengles");
        SDLActivity.nativeSetenv("FOXHOLLOW_RENDER_SCALE", allowed(intent.getStringExtra("scale"), Settings.SCALES, "1"));
        SDLActivity.nativeSetenv("FOXHOLLOW_SCREEN_STYLE", allowed(intent.getStringExtra("aspect"), Settings.ASPECTS, "narrow"));
        SDLActivity.nativeSetenv("FOXHOLLOW_FRAME_LIMIT", "60");
        SDLActivity.nativeSetenv("FOXHOLLOW_ETC2", intent.getBooleanExtra("etc2", true) ? "1" : "0");
        SDLActivity.nativeSetenv("FOXHOLLOW_VSYNC", intent.getBooleanExtra("vsync", true) ? "1" : "0");
        SDLActivity.nativeSetenv("FOXHOLLOW_AUDIO_GAIN", Float.toString(Math.max(0, Math.min(100, intent.getIntExtra("volume", 100))) / 100f));
        boolean editor = intent.getBooleanExtra("editor", false);
        SDLActivity.nativeSetenv("FOXHOLLOW_TOUCH", intent.getBooleanExtra("touch", true) || editor ? "1" : "0");
        SDLActivity.nativeSetenv("FOXHOLLOW_TOUCH_EDITOR", editor ? "1" : "0");
        SDLActivity.nativeSetenv("FOXHOLLOW_SHOW_FPS", intent.getBooleanExtra("fps", false) ? "1" : "0");
        return new String[] { new File(getFilesDir(), "disc.img").getAbsolutePath() };
    }

    @Override
    protected void onDestroy() {
        Diagnostics.gameEvent(this, "phase=activity_destroy finishing=" + isFinishing()
            + " changingConfiguration=" + isChangingConfigurations());
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
