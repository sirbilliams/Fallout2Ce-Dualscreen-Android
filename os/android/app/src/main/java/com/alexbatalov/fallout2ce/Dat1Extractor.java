package com.alexbatalov.fallout2ce;

import java.io.BufferedOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Fallout 1's big-endian directory tables and block-framed LZSS payloads. */
final class Dat1Extractor {
    private Dat1Extractor() {
    }

    /** Returns false for malformed data; actual file access errors still propagate. */
    static boolean isDat1(File dat) throws IOException {
        try (RandomAccessFile in = new RandomAccessFile(dat, "r")) {
            try {
                readIndex(in);
                return true;
            } catch (InvalidArchiveException | EOFException ex) {
                return false;
            }
        }
    }

    /**
     * Extracts requested paths case-insensitively, ignoring blank/comment lines.
     * Missing paths retain request order. Each output replaces its destination only
     * after successful decoding; failures leave existing destination files intact.
     */
    static Result extract(File dat, List<String> entries, File outDir, boolean lowerCase)
            throws IOException {
        List<String> missing = new ArrayList<>();
        int written = 0;
        try (RandomAccessFile in = new RandomAccessFile(dat, "r")) {
            Map<String, Entry> index = readIndex(in);
            File root = outDir.getCanonicalFile();
            for (String line : entries) {
                String requested = line.trim();
                if (requested.isEmpty() || requested.startsWith(";") || requested.startsWith("#")) {
                    continue;
                }
                String path = path(requested);
                Entry entry = index.get(path.toLowerCase(Locale.ROOT));
                if (entry == null) {
                    missing.add(requested);
                    continue;
                }
                File target = new File(root, lowerCase ? path.toLowerCase(Locale.ROOT) : path)
                        .getCanonicalFile();
                if (!target.getPath().startsWith(root.getPath() + File.separator)) {
                    throw invalid("Output path escapes destination: " + requested);
                }
                File parent = target.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
                    throw new IOException("Cannot create " + parent);
                }
                File temp = File.createTempFile(".dat1-", ".tmp", parent);
                try {
                    in.seek(entry.offset);
                    try (OutputStream out = new BufferedOutputStream(new FileOutputStream(temp))) {
                        Payload payload = new Payload(in, entry.packedLength());
                        if (entry.compressed()) {
                            decode(payload, out, entry);
                        } else {
                            copy(payload, out, entry.size);
                        }
                    }
                    if (!temp.renameTo(target)) {
                        throw new IOException("Cannot rename extracted file to " + target);
                    }
                    written++;
                } finally {
                    if (temp.exists() && !temp.delete()) {
                        temp.deleteOnExit();
                    }
                }
            }
        }
        return new Result(written, missing);
    }

    static final class Result {
        final int written;
        final List<String> missing;

        Result(int written, List<String> missing) {
            this.written = written;
            this.missing = missing;
        }
    }

    private static Map<String, Entry> readIndex(RandomAccessFile in) throws IOException {
        long length = in.length();
        long dirs = u32(in);
        skipHeader(in);
        // Every directory needs at least a name byte and a 16-byte file table header.
        if (dirs == 0 || dirs > (length - 16) / 18) {
            throw invalid("Invalid DAT1 directory count");
        }
        List<String> names = new ArrayList<>();
        for (long i = 0; i < dirs; i++) {
            String name = name(in);
            names.add(name.equals(".") ? "" : path(name) + "/");
        }
        Map<String, Entry> index = new HashMap<>();
        for (String dir : names) {
            long count = u32(in);
            skipHeader(in);
            if (count > (length - in.getFilePointer()) / 18) {
                throw invalid("Invalid DAT1 file count");
            }
            for (long i = 0; i < count; i++) {
                String key = dir + path(name(in));
                Entry entry = new Entry(u32(in), u32(in), u32(in), u32(in));
                if (entry.offset > length || entry.packedLength() > length - entry.offset) {
                    throw invalid("Truncated DAT1 payload: " + key);
                }
                index.put(key.toLowerCase(Locale.ROOT), entry);
            }
        }
        for (Entry entry : index.values()) {
            if (entry.offset < in.getFilePointer()) {
                throw invalid("DAT1 payload overlaps index");
            }
        }
        return index;
    }

    private static long u32(RandomAccessFile in) throws IOException {
        return in.readInt() & 0xffffffffL;
    }

    private static void skipHeader(RandomAccessFile in) throws IOException {
        in.readInt();
        in.readInt();
        in.readInt();
    }

    private static String name(RandomAccessFile in) throws IOException {
        byte[] bytes = new byte[in.readUnsignedByte()];
        in.readFully(bytes);
        for (byte b : bytes) {
            if (b < 32 || b > 126) {
                throw invalid("Invalid DAT1 name");
            }
        }
        return new String(bytes, StandardCharsets.US_ASCII);
    }

    private static String path(String name) throws IOException {
        String normalized = name.replace('\\', '/');
        for (String part : normalized.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")
                    || part.indexOf(':') >= 0 || part.indexOf('\0') >= 0) {
                throw invalid("Invalid DAT1 path: " + name);
            }
        }
        return normalized;
    }

    private static final class Entry {
        final long flags;
        final long offset;
        final long size;
        final long packed;

        Entry(long flags, long offset, long size, long packed) {
            this.flags = flags;
            this.offset = offset;
            this.size = size;
            this.packed = packed;
        }

        boolean framed() {
            return (flags & 0x40) != 0;
        }

        boolean compressed() {
            // db.cc also accepts older, unframed LZSS (flags 0 or 0x10).
            return framed() || flags == 0 || (flags & 0xf0) == 0x10;
        }

        long packedLength() {
            return compressed() ? packed : size;
        }
    }

    /** Bounded, buffered access prevents a bad block from reading the next entry. */
    private static final class Payload {
        final RandomAccessFile in;
        final byte[] buffer = new byte[32768];
        long remaining;
        int position;
        int limit;

        Payload(RandomAccessFile in, long remaining) {
            this.in = in;
            this.remaining = remaining;
        }

        int read() throws IOException {
            if (remaining == 0) {
                throw invalid("Truncated LZSS data");
            }
            if (position == limit) {
                limit = (int) Math.min(buffer.length, remaining);
                in.readFully(buffer, 0, limit);
                position = 0;
            }
            remaining--;
            return buffer[position++] & 255;
        }
    }

    private static void copy(Payload in, OutputStream out, long count) throws IOException {
        while (count > 0) {
            if (in.position == in.limit) {
                // Refill through the same bounded reader used by the decoder.
                int first = in.read();
                out.write(first);
                count--;
                continue;
            }
            int chunk = (int) Math.min(count, in.limit - in.position);
            out.write(in.buffer, in.position, chunk);
            in.position += chunk;
            in.remaining -= chunk;
            count -= chunk;
        }
    }

    private static void decode(Payload in, OutputStream out, Entry entry) throws IOException {
        byte[] ring = new byte[4096];
        long produced = 0;
        while (produced < entry.size) {
            long count = in.remaining;
            boolean raw = false;
            if (entry.framed()) {
                int header = (in.read() << 8) | in.read();
                raw = (header & 0x8000) != 0;
                count = header & 0x7fff;
            }
            if (count == 0 || count > in.remaining) {
                throw invalid("Invalid LZSS block length");
            }
            if (raw) {
                if (count > entry.size - produced) {
                    throw invalid("LZSS output exceeds original size");
                }
                copy(in, out, count);
                produced += count;
                continue;
            }
            // Matches fallout1-ce: reset the dictionary prefix on each coded block.
            // The final 18 bytes retain values written by preceding coded blocks.
            Arrays.fill(ring, 0, 4078, (byte) ' ');
            int write = 4078;
            long end = in.remaining - count;
            while (in.remaining > end) {
                int flags = in.read();
                for (int bit = 1; bit <= 128 && in.remaining > end; bit <<= 1) {
                    int offset = 0;
                    int value = 0;
                    int length = 1;
                    if ((flags & bit) != 0) {
                        value = in.read();
                    } else {
                        if (in.remaining - end < 2) {
                            throw invalid("Truncated LZSS back-reference");
                        }
                        int low = in.read();
                        int high = in.read();
                        offset = low | ((high & 0xf0) << 4);
                        length = (high & 15) + 3;
                    }
                    if (length > entry.size - produced) {
                        throw invalid("LZSS output exceeds original size");
                    }
                    for (int k = 0; k < length; k++) {
                        int next = (flags & bit) != 0 ? value : ring[(offset + k) & 4095] & 255;
                        out.write(next);
                        ring[write] = (byte) next;
                        write = (write + 1) & 4095;
                    }
                    produced += length;
                }
            }
            if (!entry.framed() && produced < entry.size) {
                throw invalid("Short LZSS output");
            }
        }
    }

    private static InvalidArchiveException invalid(String message) {
        return new InvalidArchiveException(message);
    }

    private static final class InvalidArchiveException extends IOException {
        InvalidArchiveException(String message) {
            super(message);
        }
    }
}
