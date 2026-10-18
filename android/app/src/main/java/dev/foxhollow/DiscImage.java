package dev.foxhollow;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

final class DiscImage {
    static boolean validHeader(byte[] header) {
        if (header == null || header.length < 32) return false;
        boolean compressed = (header[0] == 'R' && header[1] == 'V' && header[2] == 'Z' && header[3] == 1)
            || (header[0] == 'W' && header[1] == 'I' && header[2] == 'A' && header[3] == 1);
        boolean retail = header[0] == 'G' && header[1] == 'S' && header[2] == 'A'
            && (header[3] == 'E' || header[3] == 'P' || header[3] == 'J')
            && header[4] == '0' && header[5] == '1' && header[6] == 0
            && header[7] >= 0 && header[7] <= (header[3] == 'J' ? 0 : 1)
            && (header[28] & 255) == 0xc2 && (header[29] & 255) == 0x33
            && (header[30] & 255) == 0x9f && (header[31] & 255) == 0x3d;
        return compressed || retail;
    }

    static void validate(File file) throws IOException {
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            byte[] header = new byte[32];
            input.readFully(header);
            if (!validHeader(header)) throw new IOException("GSAE/GSAJ/GSAP ISO, RVZ, WIA");
            if (input.length() < 0x80) throw new java.io.EOFException();
            if (header[0] != 'G') return;
            if (input.length() < 0x2440) throw new java.io.EOFException();
            input.seek(0x420);
            long executable = input.readInt() & 0xffffffffL;
            long table = input.readInt() & 0xffffffffL;
            long tableSize = input.readInt() & 0xffffffffL;
            if (executable < 0x2440 || executable >= input.length() || table < 0x2440 || tableSize < 12
                || table + tableSize > input.length()) throw new IOException("ISO: DOL / FST");
        }
    }
}
