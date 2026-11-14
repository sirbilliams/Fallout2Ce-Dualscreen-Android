package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class GameInstallerSizeTest {
    @Test public void readsTotalFromContentRange() {
        assertEquals(358069166L, GameInstaller.totalFromContentRange("bytes 0-0/358069166"));
    }

    @Test public void unknownOrMissingTotalIsMinusOne() {
        assertEquals(-1L, GameInstaller.totalFromContentRange("bytes 0-0/*"));
        assertEquals(-1L, GameInstaller.totalFromContentRange(null));
        assertEquals(-1L, GameInstaller.totalFromContentRange("garbage"));
    }
}
