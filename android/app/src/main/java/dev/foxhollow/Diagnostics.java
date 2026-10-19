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

    static void gameEvent(Context context, String event) {
        try {
            LogFiles.append(new File(directory(context), "game-events.log"), new Date() + " " + event);
        } catch (java.io.IOException ignored) {}
    }

    static void captureExitInfo(Context context) {
        StringBuilder report = new StringBuilder();
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                android.app.ActivityManager manager = (android.app.ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
                for (android.app.ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 12)) {
                    if (!(context.getPackageName() + ":game").equals(info.getProcessName())) continue;
                    report.append("time=").append(new Date(info.getTimestamp())).append(" pid=").append(info.getPid())
                        .append(" reason=").append(exitReason(info.getReason())).append(" status=").append(info.getStatus())
                        .append(" importance=").append(info.getImportance()).append(" pssKiB=").append(info.getPss())
                        .append(" rssKiB=").append(info.getRss()).append("\n")
                        .append(info.getDescription()).append("\n\n");
                    if (info.getReason() == android.app.ApplicationExitInfo.REASON_ANR) {
                        try (java.io.InputStream input = info.getTraceInputStream()) {
                            if (input != null) {
                                byte[] bytes = new byte[8192];
                                int length = input.read(bytes);
                                if (length > 0) report.append(new String(bytes, 0, length, java.nio.charset.StandardCharsets.UTF_8));
                            }
                        } catch (java.io.IOException ignored) {}
                    }
                }
                if (report.length() == 0) report.append("NO_RECORDED_GAME_EXIT\n");
            } catch (RuntimeException error) {
                report.append("EXIT_INFO_UNAVAILABLE: ").append(error.getClass().getSimpleName()).append('\n');
            }
        } else report.append("EXIT_INFO_REQUIRES_ANDROID_11\n");
        try { LogFiles.replace(new File(directory(context), "exit-info.log"), report.toString()); }
        catch (java.io.IOException ignored) {}
    }

    private static String exitReason(int reason) {
        switch (reason) {
            case 1: return "EXIT_SELF";
            case 2: return "SIGNALED";
            case 3: return "LOW_MEMORY";
            case 4: return "JAVA_CRASH";
            case 5: return "NATIVE_CRASH";
            case 6: return "ANR";
            case 7: return "INITIALIZATION_FAILURE";
            case 8: return "PERMISSION_CHANGE";
            case 9: return "EXCESSIVE_RESOURCE_USAGE";
            case 10: return "USER_REQUESTED";
            case 11: return "USER_STOPPED";
            case 12: return "DEPENDENCY_DIED";
            case 13: return "OTHER";
            default: return "REASON_" + reason;
        }
    }

    static void captureLogcat(Context context) {
        captureExitInfo(context);
        File directory = directory(context);
        for (String name : new String[] {"launcher", "game"}) {
            java.lang.Process process = null;
            File temporary = null;
            try {
                int pid = Integer.parseInt(LogFiles.tail(new File(directory, name + ".pid"), 32).trim());
                if (pid <= 0) continue;
                temporary = File.createTempFile("capture-", ".tmp", directory);
                process = new ProcessBuilder("logcat", "-b", "main", "-b", "system", "-b", "crash", "-d", "-v", "threadtime", "--pid=" + pid, "-t", "1000")
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
