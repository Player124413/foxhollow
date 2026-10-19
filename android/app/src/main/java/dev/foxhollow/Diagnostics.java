package dev.foxhollow;

import android.content.Context;
import android.os.Build;
import android.os.Process;
import java.io.File;
import java.util.Date;

final class Diagnostics {
    private static boolean started;

    static File directory(Context context) {
        File directory = new File(context.getFilesDir(), "logs");
        directory.mkdirs();
        return directory;
    }

    static String header(Context context) {
        String version = "?";
        long code = 0;
        try {
            android.content.pm.PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            version = info.versionName;
            code = info.getLongVersionCode();
        } catch (android.content.pm.PackageManager.NameNotFoundException ignored) {}
        return "Foxhollow " + version + " (" + code + ")\n" + new Date() + "\nAndroid " + Build.VERSION.RELEASE
            + " / API " + Build.VERSION.SDK_INT + "\n" + Build.MANUFACTURER + " " + Build.MODEL
            + "\nABI " + android.text.TextUtils.join(",", Build.SUPPORTED_ABIS) + "\n";
    }

    static synchronized void start(Context context, String processName) {
        if (started) return;
        started = true;
        Context app = context.getApplicationContext();
        File directory = directory(app);
        File events = new File(directory, processName + "-events.log");
        File system = new File(directory, processName + ".log");
        try {
            LogFiles.rotate(events);
            LogFiles.rotate(system);
            LogFiles.append(events, header(app) + "process=" + processName + " pid=" + Process.myPid());
        } catch (java.io.IOException ignored) {}
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try { LogFiles.append(events, android.util.Log.getStackTraceString(error)); }
            catch (java.io.IOException ignored) {}
            if (previous != null) previous.uncaughtException(thread, error);
            else { Process.killProcess(Process.myPid()); System.exit(10); }
        });
        try {
            LogFiles.replace(new File(directory, processName + ".pid"), Integer.toString(Process.myPid()));
        } catch (java.io.IOException ignored) {}
    }

    static void captureLogcat(Context context) {
        File directory = directory(context);
        for (String name : new String[] {"launcher", "game"}) {
            java.lang.Process process = null;
            File temporary = null;
            try {
                int pid = Integer.parseInt(LogFiles.tail(new File(directory, name + ".pid"), 32).trim());
                if (pid <= 0) continue;
                temporary = File.createTempFile("capture-", ".tmp", directory);
                process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "--pid=" + pid, "-t", "1000")
                    .redirectErrorStream(true).redirectOutput(temporary).start();
                if (!process.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) process.destroyForcibly();
                String text = LogFiles.tail(temporary, LogFiles.FILE_LIMIT);
                if (!text.isEmpty()) LogFiles.replace(new File(directory, name + ".log"), text);
            } catch (java.io.IOException | NumberFormatException error) {
                try { LogFiles.append(new File(directory, "launcher-events.log"), "LOGCAT_UNAVAILABLE: " + error.getClass().getSimpleName()); }
                catch (java.io.IOException ignored) {}
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                return;
            } finally {
                if (process != null) process.destroy();
                if (temporary != null) temporary.delete();
            }
        }
    }

    static void gameSettings(Context context, android.content.Intent intent) {
        try {
            LogFiles.append(new File(directory(context), "game-events.log"),
                "renderer=OpenGLES scale=" + intent.getStringExtra("scale") + " aspect=" + intent.getStringExtra("aspect")
                + " etc2=" + intent.getBooleanExtra("etc2", true) + " vsync=" + intent.getBooleanExtra("vsync", true)
                + " touch=" + intent.getBooleanExtra("touch", true));
        } catch (java.io.IOException ignored) {}
    }
}
