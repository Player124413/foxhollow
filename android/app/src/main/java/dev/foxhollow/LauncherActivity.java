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
    private boolean checkingUpdates;
    private boolean loadingLogs;

    @Override
    protected void attachBaseContext(android.content.Context base) {
        android.content.res.Configuration config = new android.content.res.Configuration(base.getResources().getConfiguration());
        config.setLocale(new java.util.Locale(languageIndex(base) == 1 ? "ru" : "en"));
        super.attachBaseContext(base.createConfigurationContext(config));
    }

    private static int languageIndex(android.content.Context context) {
        try {
            return context.getSharedPreferences("launcher", MODE_PRIVATE).getInt("language", 0) == 1 ? 1 : 0;
        } catch (ClassCastException exception) {
            return 0;
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        Diagnostics.start(this, "launcher");
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
        String noticeKey = "notice_" + languageIndex(this);
        if (!getSharedPreferences("launcher", MODE_PRIVATE).getBoolean(noticeKey, false)) {
            showCommunity();
            getSharedPreferences("launcher", MODE_PRIVATE).edit().putBoolean(noticeKey, true).apply();
        }
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
        Spinner language = findViewById(R.id.language);
        language.setSelection(languageIndex(this));
        language.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position != languageIndex(LauncherActivity.this) && !importing) {
                    getSharedPreferences("launcher", MODE_PRIVATE).edit().putInt("language", position).apply();
                    recreate();
                }
            }
            public void onNothingSelected(AdapterView<?> parent) {}
        });
        findViewById(R.id.logs).setOnClickListener(view -> showLogs());
        findViewById(R.id.about).setOnClickListener(view -> showCommunity());
        findViewById(R.id.check_updates).setOnClickListener(view -> checkUpdates());
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
        bindSwitch(R.id.etc2, "etc2");
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

    private void showLogs() {
        if (loadingLogs || importing) return;
        loadingLogs = true;
        updateStatus();
        worker.execute(() -> {
            Diagnostics.captureLogcat(this);
            String report = LogFiles.report(Diagnostics.directory(this), Diagnostics.header(this));
            String concise = LogFiles.conciseReport(Diagnostics.directory(this), Diagnostics.header(this));
            runOnUiThread(() -> {
                loadingLogs = false;
                if (isFinishing() || isDestroyed()) return;
                updateStatus();
                android.widget.ScrollView scroll = new android.widget.ScrollView(this);
                TextView text = new TextView(this);
                text.setText(LogFiles.copyText(concise, getString(R.string.logs_truncated)));
                text.setTextIsSelectable(true);
                text.setTypeface(android.graphics.Typeface.MONOSPACE);
                text.setTextSize(12);
                int padding = (int)(16 * getResources().getDisplayMetrics().density);
                text.setPadding(padding, padding, padding, padding);
                scroll.addView(text);
                AlertDialog dialog = new AlertDialog.Builder(this).setTitle(R.string.logs)
                    .setMessage(R.string.logs_privacy).setView(scroll)
                    .setPositiveButton(R.string.logs_copy, null)
                    .setNeutralButton(R.string.logs_share, null)
                    .setNegativeButton(R.string.close, null).create();
                dialog.setOnShowListener(visible -> {
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
                        android.content.ClipboardManager clipboard = (android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
                        android.content.ClipData clip = android.content.ClipData.newPlainText("Foxhollow logs",
                            LogFiles.copyText(concise, getString(R.string.logs_truncated)));
                        android.os.PersistableBundle extras = new android.os.PersistableBundle();
                        extras.putBoolean("android.content.extra.IS_SENSITIVE", true);
                        clip.getDescription().setExtras(extras);
                        clipboard.setPrimaryClip(clip);
                        android.widget.Toast.makeText(this, R.string.logs_copied, android.widget.Toast.LENGTH_SHORT).show();
                    });
                    dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(button -> shareLogs(report));
                    if (dialog.getWindow() != null) dialog.getWindow().setLayout(
                        (int)(getResources().getDisplayMetrics().widthPixels * .95f),
                        (int)(getResources().getDisplayMetrics().heightPixels * .85f));
                });
                dialog.show();
            });
        });
    }

    private void shareLogs(String report) {
        try {
            File directory = new File(getCacheDir(), "shared-logs");
            if (!directory.isDirectory() && !directory.mkdirs()) throw new java.io.IOException();
            File[] old = directory.listFiles();
            if (old != null) {
                java.util.Arrays.sort(old, java.util.Comparator.comparingLong(File::lastModified));
                for (int i = 0; i < old.length - 3; ++i) if (old[i].isFile()) old[i].delete();
            }
            File file = File.createTempFile("foxhollow-", ".txt", directory);
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(report.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            Uri uri = androidx.core.content.FileProvider.getUriForFile(this, getPackageName() + ".logs", file);
            Intent share = new Intent(Intent.ACTION_SEND).setType("text/plain")
                .putExtra(Intent.EXTRA_STREAM, uri).putExtra(Intent.EXTRA_SUBJECT, "Foxhollow logs")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            share.setClipData(android.content.ClipData.newUri(getContentResolver(), "Foxhollow logs", uri));
            startActivity(Intent.createChooser(share, getString(R.string.logs_share)));
        } catch (java.io.IOException | android.content.ActivityNotFoundException error) {
            error(getString(R.string.logs_error));
        }
    }

    private void openLink(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (android.content.ActivityNotFoundException exception) {
            error(getString(R.string.link_error));
        }
    }

    private void showCommunity() {
        new AlertDialog.Builder(this).setTitle(R.string.about).setMessage(R.string.community_message)
            .setPositiveButton(R.string.telegram, (dialog, which) -> openLink("https://t.me/player1444ports"))
            .setNeutralButton(R.string.upstream, (dialog, which) -> openLink("https://github.com/JackPriceBurns/foxhollow"))
            .setNegativeButton(R.string.close, null).show();
    }

    private void checkUpdates() {
        if (checkingUpdates || importing) return;
        checkingUpdates = true;
        updateStatus();
        worker.execute(() -> {
            String tag = null;
            String releaseUrl = null;
            int result = R.string.updates_error;
            javax.net.ssl.HttpsURLConnection connection = null;
            try {
                connection = (javax.net.ssl.HttpsURLConnection)new java.net.URL(
                    "https://api.github.com/repos/Player124413/foxhollow/releases/latest").openConnection();
                connection.setConnectTimeout(10000);
                connection.setReadTimeout(10000);
                connection.setRequestProperty("Accept", "application/vnd.github+json");
                connection.setRequestProperty("User-Agent", "Foxhollow-Android");
                connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
                int status = connection.getResponseCode();
                if (status == 404) {
                    result = R.string.no_releases;
                } else if (status == 200) {
                    try (InputStream input = connection.getInputStream();
                         java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream()) {
                        byte[] buffer = new byte[8192];
                        int length;
                        while ((length = input.read(buffer)) != -1) {
                            if (Thread.currentThread().isInterrupted() || output.size() + length > 2 * 1024 * 1024)
                                throw new java.io.IOException();
                            output.write(buffer, 0, length);
                        }
                        org.json.JSONObject release = new org.json.JSONObject(output.toString("UTF-8"));
                        String candidate = release.getString("html_url");
                        if (!candidate.startsWith("https://github.com/Player124413/foxhollow/releases/tag/"))
                            throw new java.io.IOException();
                        tag = release.getString("tag_name");
                        releaseUrl = candidate;
                        result = R.string.latest_release;
                    }
                }
            } catch (Exception exception) {
                result = R.string.updates_error;
            } finally {
                if (connection != null) connection.disconnect();
            }
            final int messageId = result;
            final String version = tag;
            final String url = releaseUrl;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                checkingUpdates = false;
                updateStatus();
                String message = getString(messageId);
                if (messageId == R.string.latest_release) {
                    String installed;
                    try { installed = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
                    catch (android.content.pm.PackageManager.NameNotFoundException exception) { installed = "?"; }
                    message = version.replaceFirst("^v", "").equals(installed)
                        ? getString(R.string.up_to_date, version) : getString(R.string.latest_release, version, installed);
                }
                AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle(R.string.check_updates)
                    .setMessage(message).setNegativeButton(R.string.close, null);
                if (url != null) dialog.setPositiveButton(R.string.open_release, (window, which) -> openLink(url));
                dialog.show();
            });
        });
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
        findViewById(R.id.logs).setEnabled(!importing && !loadingLogs);
        ((TextView)findViewById(R.id.logs)).setText(loadingLogs ? R.string.logs_loading : R.string.logs);
        findViewById(R.id.language).setEnabled(!importing);
        findViewById(R.id.check_updates).setEnabled(!importing && !checkingUpdates);
        ((TextView)findViewById(R.id.check_updates)).setText(checkingUpdates ? R.string.checking_updates : R.string.check_updates);
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
