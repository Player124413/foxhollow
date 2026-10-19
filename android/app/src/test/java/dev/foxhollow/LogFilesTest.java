package dev.foxhollow;

import org.junit.Test;
import static org.junit.Assert.*;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;

public final class LogFilesTest {
    @Test public void rotationAndRetention() throws Exception {
        File directory = Files.createTempDirectory("foxhollow-logs").toFile();
        File log = new File(directory, "game.log");
        try {
            LogFiles.append(log, "previous session");
            LogFiles.rotate(log);
            assertTrue(new File(log + ".prev").isFile());
            assertFalse(log.exists());
            String large = new String(new char[20000]).replace('\0', 'X');
            for (int i = 0; i < 100; ++i) LogFiles.append(log, large);
            assertTrue(log.length() <= LogFiles.FILE_LIMIT);
            assertTrue(new File(log + ".prev").length() <= LogFiles.FILE_LIMIT);
        } finally {
            for (File file : directory.listFiles()) file.delete();
            directory.delete();
        }
    }

    @Test public void utf8TailAndClipboardLimit() throws Exception {
        File file = File.createTempFile("foxhollow-log", ".txt");
        try {
            Files.write(file.toPath(), "Привет мир".getBytes(StandardCharsets.UTF_8));
            assertEquals("мир", LogFiles.tail(file, 6));
            String text = new String(new char[LogFiles.COPY_LIMIT + 10000]).replace('\0', 'X') + "END";
            String copied = LogFiles.copyText(text, "TRUNCATED");
            assertTrue(copied.contains("TRUNCATED"));
            assertTrue(copied.endsWith("END"));
            assertTrue(copied.length() < 64000);
            assertEquals("short", LogFiles.copyText("short", "TRUNCATED"));
        } finally { file.delete(); }
    }

    @Test public void reportOnlyIncludesDiagnosticSources() throws Exception {
        File directory = Files.createTempDirectory("foxhollow-report").toFile();
        try {
            LogFiles.append(new File(directory, "native.log"), "native error");
            LogFiles.append(new File(directory, "game-events.log"), "ETC2=true");
            LogFiles.append(new File(directory, "private-save.bin"), "PRIVATE");
            String report = LogFiles.report(directory, "HEADER");
            assertTrue(report.startsWith("HEADER"));
            assertTrue(report.contains("native error"));
            assertTrue(report.contains("ETC2=true"));
            assertFalse(report.contains("PRIVATE"));
        } finally {
            for (File file : directory.listFiles()) file.delete();
            directory.delete();
        }
    }
}
