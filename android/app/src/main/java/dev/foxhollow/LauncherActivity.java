package dev.foxhollow;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.system.Os;
import android.view.View;
import android.widget.AdapterView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class LauncherActivity extends Activity {
    private static final int PICK_DISC = 1;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile boolean cancelImport;
    private boolean importing;
    private Settings settings;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (android.os.Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        settings = new Settings(this);
        worker.execute(() -> {
            File[] files = getFilesDir().listFiles();
            if (files != null) for (File file : files) {
                if (file.getName().startsWith("disc-") && file.getName().endsWith(".import")) file.delete();
            }
        });
        showLauncher();
    }

    private void showLauncher() {
        setContentView(R.layout.launcher);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            View content = findViewById(android.R.id.content);
            content.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars()
                    | android.view.WindowInsets.Type.displayCutout());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
            content.requestApplyInsets();
        }
        findViewById(R.id.play).setOnClickListener(view -> {
            if (new File(getFilesDir(), "disc.img").isFile()) {
                view.setEnabled(false);
                startActivity(settings.gameIntent(this));
            }
        });
        findViewById(R.id.import_disc).setOnClickListener(view -> choose());
        findViewById(R.id.import_cancel).setOnClickListener(view -> {
            cancelImport = true;
        });
        bindSpinner(R.id.render_scale, "scale", Settings.SCALES.length, 1);
        bindSpinner(R.id.aspect, "aspect", Settings.ASPECTS.length, 0);
        bindSwitch(R.id.vsync, "vsync");
        bindSwitch(R.id.fps, "fps", false);
        bindSwitch(R.id.touch, "touch");
        bindSwitch(R.id.touch_editor, "editor", false);
        bindSwitch(R.id.keep_screen_on, "keepScreenOn");
        SeekBar volume = findViewById(R.id.volume);
        volume.setProgress(settings.index("volume", 101, 100));
        volumeLabel(volume.getProgress());
        volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                volumeLabel(value);
                if (fromUser) settings.put("volume", value);
            }
            public void onStartTrackingTouch(SeekBar bar) {}
            public void onStopTrackingTouch(SeekBar bar) {}
        });
        findViewById(R.id.settings_reset).setOnClickListener(view -> confirm(() -> {
            settings.reset(); showLauncher();
        }));
        findViewById(R.id.touch_reset).setOnClickListener(view -> confirm(() -> {
            File file = new File(getFilesDir(), "touch.json");
            if (file.exists() && !file.delete()) error(getString(R.string.io_error));
        }));
        findViewById(R.id.cache_clear).setOnClickListener(view -> confirm(() -> {
            if (!deleteChildren(getCacheDir())) error(getString(R.string.io_error));
        }));
        updateStatus();
    }

    private void bindSpinner(int id, String key, int count, int fallback) {
        Spinner spinner = findViewById(id);
        spinner.setSelection(settings.index(key, count, fallback));
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> parent, View view, int position, long itemId) {
                settings.put(key, position);
            }
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void bindSwitch(int id, String key) { bindSwitch(id, key, true); }
    private void bindSwitch(int id, String key, boolean fallback) {
        Switch widget = findViewById(id);
        widget.setChecked(settings.flag(key, fallback));
        widget.setOnCheckedChangeListener((button, checked) -> settings.put(key, checked));
    }

    private void volumeLabel(int value) {
        ((TextView)findViewById(R.id.volume_label)).setText(getString(R.string.volume, value));
    }

    private void confirm(Runnable action) {
        new AlertDialog.Builder(this).setMessage(R.string.confirm)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok, (dialog, which) -> action.run()).show();
    }

    private void error(String message) {
        if (!isFinishing() && !isDestroyed()) new AlertDialog.Builder(this).setMessage(message)
            .setPositiveButton(android.R.string.ok, null).show();
    }

    private static boolean deleteChildren(File root) {
        File[] files = root.listFiles();
        if (files == null) return false;
        boolean ok = true;
        for (File file : files) {
            if (!java.nio.file.Files.isSymbolicLink(file.toPath()) && file.isDirectory()) ok &= deleteChildren(file);
            ok &= file.delete();
        }
        return ok;
    }

    private void updateStatus() {
        File disc = new File(getFilesDir(), "disc.img");
        findViewById(R.id.play).setEnabled(!importing && disc.isFile());
        findViewById(R.id.import_disc).setEnabled(!importing);
        findViewById(R.id.import_progress).setVisibility(importing ? View.VISIBLE : View.GONE);
        findViewById(R.id.import_cancel).setVisibility(importing ? View.VISIBLE : View.GONE);
        if (!importing) ((TextView)findViewById(R.id.disc_status)).setText(disc.isFile()
            ? getString(R.string.disc_ready, android.text.format.Formatter.formatFileSize(this, disc.length()))
            : getString(R.string.disc_missing));
    }

    private void choose() {
        if (importing) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_DISC);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_DISC || importing || result != RESULT_OK || data == null || data.getData() == null) return;
        importing = true;
        updateStatus();
        Uri uri = data.getData();
        cancelImport = false;
        worker.submit(() -> {
            File temporary = null;
            String failure = null;
            try {
                temporary = File.createTempFile("disc-", ".import", getFilesDir());
                try (InputStream input = getContentResolver().openInputStream(uri);
                     FileOutputStream output = new FileOutputStream(temporary)) {
                    if (input == null) throw new java.io.IOException();
                    byte[] buffer = new byte[1024 * 1024];
                    long total = 0, lastProgress = 0;
                    int length;
                    while ((length = input.read(buffer)) != -1) {
                        if (cancelImport || Thread.currentThread().isInterrupted()) throw new InterruptedException();
                        output.write(buffer, 0, length);
                        total += length;
                        long now = SystemClock.elapsedRealtime();
                        if (now - lastProgress >= 250) {
                            lastProgress = now;
                            long copied = total / (1024 * 1024);
                            runOnUiThread(() -> {
                                if (!isDestroyed()) ((TextView)findViewById(R.id.disc_status))
                                    .setText(getString(R.string.import_progress, copied));
                            });
                        }
                    }
                    output.getFD().sync();
                }
                DiscImage.validate(temporary);
                if (cancelImport || Thread.currentThread().isInterrupted()) throw new InterruptedException();
                Os.rename(temporary.getAbsolutePath(), new File(getFilesDir(), "disc.img").getAbsolutePath());
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
            } catch (Exception exception) {
                failure = exception.toString();
            } finally {
                if (temporary != null) temporary.delete();
            }
            String finalFailure = failure;
            runOnUiThread(() -> {
                importing = false;
                if (!isDestroyed() && !isFinishing()) {
                    updateStatus();
                    if (finalFailure != null) error(getString(R.string.import_error, finalFailure));
                }
            });
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (settings != null) updateStatus();
    }

    @Override
    protected void onDestroy() {
        cancelImport = true;
        worker.shutdownNow();
        super.onDestroy();
    }
}
