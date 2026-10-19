package dev.foxhollow;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

final class LogFiles {
    static final int FILE_LIMIT = 512 * 1024;
    static final int COPY_LIMIT = 60000;
    static final String[] SOURCES = {"exit-info.log", "game-events.log", "native.log", "game.log", "launcher-events.log", "launcher.log"};

    static synchronized void rotate(File file) throws IOException {
        File previous = new File(file.getPath() + ".prev");
        if (file.exists()) {
            if (previous.exists() && !previous.delete()) throw new IOException("LOG_ROTATE");
            if (!file.renameTo(previous)) throw new IOException("LOG_ROTATE");
        }
    }

    static synchronized void append(File file, String text) throws IOException {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (file.length() + Math.min(bytes.length, 16384) + 1 > FILE_LIMIT) rotate(file);
        try (FileOutputStream out = new FileOutputStream(file, true)) {
            out.write(bytes, 0, Math.min(bytes.length, 16384));
            out.write('\n');
        }
    }

    static synchronized void replace(File file, String text) throws IOException {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream out = new FileOutputStream(file)) {
            int offset = Math.max(0, bytes.length - FILE_LIMIT);
            out.write(bytes, offset, bytes.length - offset);
        }
    }

    static String tail(File file, int maximum) throws IOException {
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            int length = (int)Math.min(input.length(), maximum);
            input.seek(input.length() - length);
            byte[] bytes = new byte[length];
            input.readFully(bytes);
            int start = 0;
            while (start < bytes.length && (bytes[start] & 0xc0) == 0x80) ++start;
            return new String(bytes, start, bytes.length - start, StandardCharsets.UTF_8);
        }
    }

    static String report(File directory, String header) {
        StringBuilder report = new StringBuilder(header);
        for (String name : SOURCES) {
            for (String suffix : new String[] {".prev", ""}) {
                File file = new File(directory, name + suffix);
                if (!file.isFile()) continue;
                report.append("\n\n=== ").append(file.getName()).append(" ===\n");
                try { report.append(tail(file, 128 * 1024)); }
                catch (IOException error) { report.append("LOG_READ_ERROR\n"); }
            }
        }
        return report.toString();
    }

    static String conciseReport(File directory, String header) {
        StringBuilder report = new StringBuilder(header);
        String[] files = {"game-events.log", "exit-info.log", "native.log", "game.log", "native.log.prev"};
        int[] sizes = {4096, 12288, 16384, 12288, 8192};
        for (int i = 0; i < files.length; ++i) {
            File file = new File(directory, files[i]);
            if (!file.isFile()) continue;
            report.append("\n\n=== ").append(files[i]).append(" ===\n");
            try {
                String text = tail(file, files[i].equals("game.log") ? FILE_LIMIT : sizes[i]);
                if (files[i].equals("game.log")) {
                    text = text.replaceAll("(?m)^.*\\s[VD]\\s+(?:MiuiMultiWindowUtils|VRI\\[[^\\]]*\\]|FramePredict|ScrollIdentify|ScrollerOptimizationManager)\\s*:.*(?:\\r?\\n|$)", "");
                    if (text.length() > sizes[i]) text = text.substring(text.length() - sizes[i]);
                }
                report.append(text);
            } catch (IOException error) { report.append("LOG_READ_ERROR\n"); }
        }
        return report.toString();
    }

    static String copyText(String report, String notice) {
        if (report.length() <= COPY_LIMIT) return report;
        int start = report.length() - COPY_LIMIT;
        if (Character.isLowSurrogate(report.charAt(start))) ++start;
        return report.substring(0, Math.min(1000, report.length())) + "\n\n" + notice + "\n\n" + report.substring(start);
    }
}
