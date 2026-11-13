package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.Assert.*;

public class Dat1ExtractorTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    @Test public void detectsDat1AndRejectsGarbage() throws Exception {
        assertTrue(Dat1Extractor.isDat1(archive(stored("hello.txt", bytes("hello")))));
        assertFalse(Dat1Extractor.isDat1(write(new byte[0])));
        assertFalse(Dat1Extractor.isDat1(write(bytes("not a DAT1 archive"))));
        assertFalse(Dat1Extractor.isDat1(write(new byte[] {1, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0})));
        byte[] hugeCount = new byte[64];
        Arrays.fill(hugeCount, 0, 4, (byte) 255);
        assertFalse(Dat1Extractor.isDat1(write(hugeCount)));
    }

    @Test public void storedRoundTripAndOverwrite() throws Exception {
        byte[] data = new byte[200000];
        for (int i = 0; i < data.length; i++) data[i] = (byte) (i * 31);
        File dat = archive(stored("ART\\SCENERY\\DOOR04.FRM", data));
        File out = temp.newFolder();
        File target = new File(out, "art/scenery/door04.frm");
        assertTrue(target.getParentFile().mkdirs());
        Files.write(target.toPath(), bytes("old"));
        Dat1Extractor.Result result = Dat1Extractor.extract(dat,
                Collections.singletonList("ART\\SCENERY\\DOOR04.FRM"), out, true);
        assertEquals(1, result.written);
        assertTrue(result.missing.isEmpty());
        assertArrayEquals(data, Files.readAllBytes(target.toPath()));
        assertEquals(1, target.getParentFile().list().length);
    }

    @Test public void literalsOverlappingReferencesInitialSpacesAndRawBlocks() throws Exception {
        // Initial spaces, ABC, then an overlapping six-byte reference to ABC.
        byte[] first = {0x0e, 0, 0, 'A', 'B', 'C', (byte) 0xf1, (byte) 0xf3};
        byte[] second = {0, 0, 0}; // prefix must reset to spaces
        File dat = archive(new Item("DATA\\TEST.BIN", 0x40, 19,
                blocks(first, bytes("raw!"), second)));
        File out = temp.newFolder();
        assertEquals(1, Dat1Extractor.extract(dat, Collections.singletonList("data/test.bin"),
                out, true).written);
        assertArrayEquals(bytes("   ABCABCABCraw!   "),
                Files.readAllBytes(new File(out, "data/test.bin").toPath()));
    }

    @Test public void sourceRawBlockUsesHighBitAndLength() throws Exception {
        // db.cc clears bit 15: raw length 3 is 0x8003, not signed -3 (0xfffd).
        File dat = archive(new Item("raw", 0x40, 3,
                new byte[] {(byte) 0x80, 3, 1, 2, 3}));
        File out = temp.newFolder();
        Dat1Extractor.extract(dat, Collections.singletonList("raw"), out, true);
        assertArrayEquals(new byte[] {1, 2, 3}, Files.readAllBytes(new File(out, "raw").toPath()));
    }

    @Test public void dictionaryTailRetainedBetweenCodedBlocks() throws Exception {
        File dat = archive(new Item("tail", 0x40, 6,
                blocks(new byte[] {7, 'A', 'B', 'C'}, null,
                        new byte[] {0, (byte) 0xee, (byte) 0xf0})));
        File out = temp.newFolder();
        Dat1Extractor.extract(dat, Collections.singletonList("tail"), out, false);
        assertArrayEquals(bytes("ABCABC"), Files.readAllBytes(new File(out, "tail").toPath()));
    }

    @Test public void legacyUnframedLzss() throws Exception {
        for (int flags : new int[] {0, 0x10}) {
            File dat = archive(new Item("old", flags, 6,
                    new byte[] {7, 'A', 'B', 'C', (byte) 0xee, (byte) 0xf0}));
            File out = temp.newFolder();
            Dat1Extractor.extract(dat, Collections.singletonList("old"), out, true);
            assertArrayEquals(bytes("ABCABC"), Files.readAllBytes(new File(out, "old").toPath()));
        }
    }

    @Test public void matchingCommentsAndOutputCase() throws Exception {
        File dat = archive(stored("ART\\SCENERY\\DOOR04.FRM", bytes("door")),
                stored("ROOT.TXT", bytes("root")));
        List<String> requests = Arrays.asList("", " \r", " ; skip", " # skip\r",
                " ArT/Scenery\\Door04.Frm\r ", " Root.TxT ");
        File mixed = temp.newFolder();
        assertEquals(2, Dat1Extractor.extract(dat, requests, mixed, false).written);
        assertArrayEquals(bytes("door"), Files.readAllBytes(
                new File(mixed, "ArT/Scenery/Door04.Frm").toPath()));
        assertTrue(new File(mixed, "Root.TxT").isFile());
        File lower = temp.newFolder();
        assertEquals(2, Dat1Extractor.extract(dat, requests, lower, true).written);
        assertTrue(new File(lower, "art/scenery/door04.frm").isFile());
        assertTrue(new File(lower, "root.txt").isFile());
    }

    @Test public void missingEntriesKeepListOrder() throws Exception {
        File dat = archive(stored("found", bytes("yes")));
        Dat1Extractor.Result result = Dat1Extractor.extract(dat,
                Arrays.asList(" Z\\Absent ", "found", "Other/Missing", "Z\\Absent"),
                temp.newFolder(), true);
        assertEquals(1, result.written);
        assertEquals(Arrays.asList("Z\\Absent", "Other/Missing", "Z\\Absent"), result.missing);
    }

    @Test public void rejectsTruncatedIndexAndPayloadWithoutPartialFiles() throws Exception {
        File dat = archive(stored("file", bytes("content")));
        try (RandomAccessFile file = new RandomAccessFile(dat, "rw")) {
            file.setLength(file.length() - 1);
        }
        assertExtractionFails(dat, "file", false);
        assertFalse(Dat1Extractor.isDat1(dat));
        File index = archive(stored("file", bytes("content")));
        try (RandomAccessFile file = new RandomAccessFile(index, "rw")) {
            file.setLength(22);
        }
        assertExtractionFails(index, "file", false);
        assertFalse(Dat1Extractor.isDat1(index));
        assertExtractionFails(write(bytes("garbage")), "file", false);
    }

    @Test public void corruptLzssLeavesNoPartialAndPreservesExistingFile() throws Exception {
        byte[][] invalid = {
                {0, 3, 1, 'A', 0}, // partial reference after a literal
                {0, 4, 1, 'A'}, // block exceeds packed size
                {0, 2, 1, 'A', 0, 0}, // early terminator
                {(byte) 0x80, 1, 'A'}, // short raw output
                {0, 3, 0, 0, 15}, // output exceeds original size
                {0, 1}, // truncated block
                {0, 0}
        };
        for (byte[] packed : invalid) {
            File dat = archive(new Item("file", 0x40, 4, packed));
            assertExtractionFails(dat, "file", false);
            assertExtractionFails(dat, "file", true);
        }
    }

    @Test public void rejectsTraversalAndAbsoluteOutputPaths() throws Exception {
        File dat = archive(stored("file", bytes("ok")));
        for (String request : Arrays.asList("../file", "/file", "C:\\file", "a/../../file")) {
            assertExtractionFails(dat, request, false);
        }
    }

    @Test public void emptyStoredEntry() throws Exception {
        File dat = archive(stored("empty", new byte[0]));
        File out = temp.newFolder();
        assertEquals(1, Dat1Extractor.extract(dat, Collections.singletonList("empty"),
                out, true).written);
        assertEquals(0, new File(out, "empty").length());
    }

    private void assertExtractionFails(File dat, String request, boolean existing) throws Exception {
        File out = temp.newFolder();
        File target = new File(out, "file");
        if (existing) Files.write(target.toPath(), bytes("previous"));
        try {
            Dat1Extractor.extract(dat, Collections.singletonList(request), out, true);
            fail("Expected IOException");
        } catch (IOException expected) {
            assertEquals(existing ? 1 : 0, out.list().length);
            if (existing) assertArrayEquals(bytes("previous"), Files.readAllBytes(target.toPath()));
        }
    }

    private static byte[] bytes(String text) {
        return text.getBytes(StandardCharsets.US_ASCII);
    }

    private static Item stored(String path, byte[] data) {
        return new Item(path, 0x20, data.length, data);
    }

    private static final class Item {
        final String path;
        final int flags;
        final int size;
        final byte[] data;

        Item(String path, int flags, int size, byte[] data) {
            this.path = path;
            this.flags = flags;
            this.size = size;
            this.data = data;
        }
    }

    private static byte[] blocks(byte[] first, byte[] raw, byte[] second) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeShort(first.length);
        out.write(first);
        if (raw != null) {
            out.writeShort(0x8000 | raw.length);
            out.write(raw);
        }
        out.writeShort(second.length);
        out.write(second);
        out.writeShort(0);
        return bytes.toByteArray();
    }

    private File archive(Item... items) throws IOException {
        Map<String, Map<String, Item>> dirs = new LinkedHashMap<>();
        dirs.put(".", new LinkedHashMap<>());
        for (Item item : items) {
            int slash = item.path.lastIndexOf('\\');
            String dir = slash < 0 ? "." : item.path.substring(0, slash);
            if (!dirs.containsKey(dir)) dirs.put(dir, new LinkedHashMap<>());
            dirs.get(dir).put(item.path.substring(slash + 1), item);
        }
        int offset = 16;
        for (Map.Entry<String, Map<String, Item>> dir : dirs.entrySet()) {
            offset += 1 + bytes(dir.getKey()).length + 16;
            for (String name : dir.getValue().keySet()) offset += 1 + bytes(name).length + 16;
        }
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        header(out, dirs.size());
        for (String dir : dirs.keySet()) name(out, dir);
        for (Map<String, Item> files : dirs.values()) {
            header(out, files.size());
            for (Map.Entry<String, Item> file : files.entrySet()) {
                Item item = file.getValue();
                name(out, file.getKey());
                out.writeInt(item.flags);
                out.writeInt(offset);
                out.writeInt(item.size);
                out.writeInt((item.flags & 0x20) != 0 ? 0 : item.data.length);
                offset += item.data.length;
            }
        }
        for (Map<String, Item> files : dirs.values()) {
            for (Item item : files.values()) out.write(item.data);
        }
        return write(bytes.toByteArray());
    }

    private static void header(DataOutputStream out, int count) throws IOException {
        out.writeInt(count);
        out.writeInt(count + 29);
        out.writeInt(0);
        out.writeInt(0x02021590);
    }

    private static void name(DataOutputStream out, String name) throws IOException {
        out.writeByte(bytes(name).length);
        out.write(bytes(name));
    }

    private File write(byte[] bytes) throws IOException {
        File file = temp.newFile();
        Files.write(file.toPath(), bytes);
        return file;
    }
}
