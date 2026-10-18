package dev.foxhollow;

import org.junit.Test;
import static org.junit.Assert.*;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public final class DiscImageTest {
    private byte[] retail(char region, int revision) {
        byte[] header = new byte[32];
        header[0] = 'G'; header[1] = 'S'; header[2] = 'A'; header[3] = (byte)region;
        header[4] = '0'; header[5] = '1'; header[7] = (byte)revision;
        header[28] = (byte)0xc2; header[29] = 0x33; header[30] = (byte)0x9f; header[31] = 0x3d;
        return header;
    }

    @Test public void supportedDiscs() {
        for (char region : new char[] {'E', 'P', 'J'}) {
            assertTrue(DiscImage.validHeader(retail(region, 0)));
            assertEquals(region != 'J', DiscImage.validHeader(retail(region, 1)));
        }
    }

    @Test public void rejectsUnsupported() {
        assertFalse(DiscImage.validHeader(null));
        assertFalse(DiscImage.validHeader(new byte[4]));
        assertFalse(DiscImage.validHeader(new byte[32]));
        assertFalse(DiscImage.validHeader(retail('K', 0)));
        assertFalse(DiscImage.validHeader(retail('E', 2)));
        byte[] header = retail('E', 0);
        header[6] = 1;
        assertFalse(DiscImage.validHeader(header));
        header = retail('P', 0);
        header[28] = 0;
        assertFalse(DiscImage.validHeader(header));
    }

    @Test public void compressedContainers() {
        for (String magic : new String[] {"RVZ", "WIA"}) {
            byte[] header = new byte[32];
            for (int i = 0; i < 3; ++i) header[i] = (byte)magic.charAt(i);
            header[3] = 1;
            assertTrue(DiscImage.validHeader(header));
            header[3] = 0;
            assertFalse(DiscImage.validHeader(header));
        }
    }

    @Test(expected = IOException.class) public void truncatedFile() throws Exception {
        File file = File.createTempFile("disc", ".img");
        try { DiscImage.validate(file); }
        finally { file.delete(); }
    }

    @Test(expected = IOException.class) public void truncatedRetail() throws Exception {
        File file = File.createTempFile("disc", ".img");
        try {
            try (FileOutputStream output = new FileOutputStream(file)) { output.write(retail('E', 0)); }
            DiscImage.validate(file);
        } finally { file.delete(); }
    }

    @Test(expected = IOException.class) public void invalidFilesystem() throws Exception {
        File file = File.createTempFile("disc", ".img");
        try {
            try (java.io.RandomAccessFile output = new java.io.RandomAccessFile(file, "rw")) {
                output.write(retail('E', 0)); output.setLength(0x3000);
                output.seek(0x420); output.writeInt(0x2440);
                output.writeInt(0x2800); output.writeInt(Integer.MAX_VALUE);
            }
            DiscImage.validate(file);
        } finally { file.delete(); }
    }

    @Test public void fileValidation() throws Exception {
        File file = File.createTempFile("disc", ".img");
        try {
            try (java.io.RandomAccessFile output = new java.io.RandomAccessFile(file, "rw")) {
                output.write(retail('E', 0));
                output.setLength(0x3000);
                output.seek(0x420);
                output.writeInt(0x2440);
                output.writeInt(0x2800);
                output.writeInt(12);
            }
            DiscImage.validate(file);
        } finally { file.delete(); }
    }
}
