#include "max7219.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX7219";

/* ---- MAX7219 register addresses ---- */
#define REG_NOOP 0x00
#define REG_DIGIT0 0x01
#define REG_DECODE_MODE 0x09
#define REG_INTENSITY 0x0A
#define REG_SCAN_LIMIT 0x0B
#define REG_SHUTDOWN 0x0C
#define REG_TEST 0x0F

/* ---- Low-level SPI bit-bang ---- */

static void spi_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(MAX7219_DIN_PIN, (byte >> i) & 1);
        gpio_set_level(MAX7219_CLK_PIN, 1);
        gpio_set_level(MAX7219_CLK_PIN, 0);
    }
}

static void max7219_write(uint8_t reg, uint8_t data) {
    gpio_set_level(MAX7219_CS_PIN, 0);
    spi_send_byte(reg);
    spi_send_byte(data);
    gpio_set_level(MAX7219_CS_PIN, 1);
}

/* ---- Public API ---- */

void max7219_display(const uint8_t fb[8]) {
    for (int row = 0; row < 8; row++) {
        max7219_write(REG_DIGIT0 + row, fb[row]);
    }
}

void max7219_clear(void) {
    uint8_t blank[8] = {0};
    max7219_display(blank);
}

void max7219_set_brightness(uint8_t level) {
    if (level > 15)
        level = 15;
    max7219_write(REG_INTENSITY, level);
}

/* ---- Effect helpers ---- */

static void show_fb(const uint8_t fb[8], int delay_ms) {
    max7219_display(fb);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

/* --- Effect 1: Expanding / contracting rings from center --- */
static void effect_rings(void) {
    /* 5 ring masks: center-out then back */
    static const uint8_t rings[5][8] = {
        {0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00}, /* 2x2 */
        {0x00, 0x00, 0x3C, 0x24, 0x24, 0x3C, 0x00, 0x00}, /* 4x4 ring */
        {0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00}, /* 6x6 ring */
        {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF}, /* 8x8 ring */
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* full */
    };
    for (int rep = 0; rep < 3; rep++) {
        for (int i = 0; i < 5; i++) {
            show_fb(rings[i], 80);
        }
        for (int i = 3; i >= 0; i--) {
            show_fb(rings[i], 80);
        }
    }
}

/* --- Effect 2: Rotating propeller / pinwheel --- */
static void effect_propeller(void) {
    static const uint8_t frames[8][8] = {
        {0x01, 0x02, 0x04, 0xFF, 0xFF, 0x20, 0x40, 0x80}, /* 0° */
        {0x00, 0x03, 0x0C, 0x30, 0xC0, 0x30, 0x0C, 0x03}, /* diagonal */
        {0x80, 0x40, 0x20, 0xFF, 0xFF, 0x04, 0x02, 0x01}, /* 180° */
        {0x00, 0xC0, 0x30, 0x0C, 0x03, 0x0C, 0x30, 0xC0}, /* other diag */
        {0x01, 0x02, 0x04, 0xFF, 0xFF, 0x20, 0x40, 0x80}, {0x00, 0x03, 0x0C, 0x30, 0xC0, 0x30, 0x0C, 0x03},
        {0x80, 0x40, 0x20, 0xFF, 0xFF, 0x04, 0x02, 0x01}, {0x00, 0xC0, 0x30, 0x0C, 0x03, 0x0C, 0x30, 0xC0},
    };
    for (int rep = 0; rep < 4; rep++) {
        for (int i = 0; i < 8; i++) {
            show_fb(frames[i], 60);
        }
    }
}

/* --- Effect 3: Rain — columns of pixels falling down --- */
static void effect_rain(void) {
    uint8_t cols[8] = {0}; /* bit-position of the raindrop in each column */
    uint8_t active[8];
    for (int c = 0; c < 8; c++)
        active[c] = (c % 2 == 0) ? 1 : 0;

    for (int step = 0; step < 48; step++) {
        uint8_t fb[8] = {0};
        for (int c = 0; c < 8; c++) {
            if (active[c]) {
                int row = cols[c];
                /* draw 2-pixel tail */
                fb[row] |= (0x80 >> c);
                if (row > 0)
                    fb[row - 1] |= (0x80 >> c);
                cols[c] = (cols[c] + 1) % 8;
                if (cols[c] == 0)
                    active[c] = (step % 3 != 0); /* random-ish gaps */
            }
        }
        show_fb(fb, 60);
        /* stagger column activations */
        if (step % 4 == 0) {
            for (int c = 0; c < 8; c++)
                if (!active[c])
                    active[c] = (step / 4 + c) % 3 == 0;
        }
    }
}

/* --- Effect 4: Checkerboard blink --- */
static void effect_checkerboard(void) {
    static const uint8_t chkA[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
    static const uint8_t chkB[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
    for (int i = 0; i < 8; i++) {
        show_fb((i % 2 == 0) ? chkA : chkB, 120);
    }
}

/* --- Effect 5: Horizontal wipe in then out --- */
static void effect_wipe(void) {
    uint8_t fb[8] = {0};
    /* fill column by column left→right */
    for (int c = 0; c < 8; c++) {
        for (int r = 0; r < 8; r++)
            fb[r] |= (0x80 >> c);
        show_fb(fb, 50);
    }
    /* erase column by column left→right */
    for (int c = 0; c < 8; c++) {
        for (int r = 0; r < 8; r++)
            fb[r] &= ~(0x80 >> c);
        show_fb(fb, 50);
    }
}

/* --- Effect 6: Spiraling inward fill --- */
static void effect_spiral(void) {
    /* Pre-computed spiral order for an 8x8 grid (row, col) */
    static const uint8_t spiral[64][2] = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7},
        {6, 7}, {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0},
        {2, 0}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6},
        {6, 5}, {6, 4}, {6, 3}, {6, 2}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5},
        {3, 5}, {4, 5}, {5, 5}, {5, 4}, {5, 3}, {5, 2}, {4, 2}, {3, 2}, {3, 3}, {3, 4}, {4, 4}, {4, 3},
    };
    uint8_t fb[8] = {0};
    for (int i = 0; i < 64; i++) {
        fb[spiral[i][0]] |= (0x80 >> spiral[i][1]);
        show_fb(fb, 25);
    }
    for (int i = 63; i >= 0; i--) {
        fb[spiral[i][0]] &= ~(0x80 >> spiral[i][1]);
        show_fb(fb, 25);
    }
}

/* --- Effect 7: Bouncing ball --- */
static void effect_bounce(void) {
    int r = 0, c = 0, dr = 1, dc = 1;
    for (int step = 0; step < 80; step++) {
        uint8_t fb[8] = {0};
        fb[r] |= (0x80 >> c);
        show_fb(fb, 40);
        r += dr;
        c += dc;
        if (r < 0) {
            r = 1;
            dr = 1;
        }
        if (r > 7) {
            r = 6;
            dr = -1;
        }
        if (c < 0) {
            c = 1;
            dc = 1;
        }
        if (c > 7) {
            c = 6;
            dc = -1;
        }
    }
}

/* --- Effect 8: Heartbeat pulse (brightness sweep on full grid) --- */
static void effect_heartbeat(void) {
    static const uint8_t full[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t ramp[] = {0, 1, 2, 4, 6, 9, 12, 15, 12, 9, 6, 4, 2, 1, 0};
    for (int beat = 0; beat < 3; beat++) {
        for (int i = 0; i < (int)(sizeof(ramp)); i++) {
            max7219_write(REG_INTENSITY, ramp[i]);
            show_fb(full, 35);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    max7219_write(REG_INTENSITY, 0x08); /* restore medium brightness */
}

/* --- Effect 9: Rotating diagonal cross --- */
static void effect_cross_rotate(void) {
    static const uint8_t frames[4][8] = {
        /* X (diagonals) */
        {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},
        /* + (axis cross) */
        {0x18, 0x18, 0x18, 0xFF, 0xFF, 0x18, 0x18, 0x18},
        /* back to X */
        {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},
        /* + again */
        {0x18, 0x18, 0x18, 0xFF, 0xFF, 0x18, 0x18, 0x18},
    };
    for (int rep = 0; rep < 3; rep++) {
        for (int i = 0; i < 4; i++) {
            show_fb(frames[i], 120);
        }
    }
}

/* --- Effect 10: Random sparkle --- */
static void effect_sparkle(void) {
    /* Simple LCG PRNG seeded with a constant for determinism */
    uint32_t rng = 0xDEADBEEF;
    for (int step = 0; step < 60; step++) {
        uint8_t fb[8] = {0};
        for (int i = 0; i < 16; i++) {
            rng = rng * 1664525u + 1013904223u;
            uint8_t row = (rng >> 24) & 0x07;
            uint8_t col = (rng >> 16) & 0x07;
            fb[row] |= (0x80 >> col);
        }
        show_fb(fb, 50);
    }
}

/* --- Effect 11: Vertical scanner (KITT-style sweeping bar) --- */
static void effect_scanner(void) {
    for (int rep = 0; rep < 3; rep++) {
        for (int r = 0; r < 8; r++) {
            uint8_t fb[8] = {0};
            fb[r] = 0xFF;
            show_fb(fb, 60);
        }
        for (int r = 6; r >= 1; r--) {
            uint8_t fb[8] = {0};
            fb[r] = 0xFF;
            show_fb(fb, 60);
        }
    }
}

/* --- Effect 12: Fire — pixels rise from the bottom --- */
static void effect_fire(void) {
    uint8_t fb[8] = {0};
    uint32_t rng = 0xCAFEBABE;
    for (int step = 0; step < 64; step++) {
        /* propagate upward with slight horizontal spread */
        for (int r = 0; r < 7; r++)
            fb[r] = (uint8_t)(fb[r + 1] | (fb[r + 1] << 1) | (fb[r + 1] >> 1));
        /* random bottom row */
        rng = rng * 1664525u + 1013904223u;
        fb[7] = (uint8_t)(rng >> 16);
        show_fb(fb, 55);
    }
    /* cool down: scroll out upward */
    for (int step = 0; step < 8; step++) {
        for (int r = 0; r < 7; r++)
            fb[r] = fb[r + 1];
        fb[7] = 0;
        show_fb(fb, 55);
    }
}

/* --- Effect 13: Wave — sinusoidal column bar scrolling --- */
static void effect_wave(void) {
    /* heights[n] = number of bottom rows lit for that phase position */
    static const uint8_t heights[16] = {4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6, 5};
    for (int offset = 0; offset < 48; offset++) {
        uint8_t fb[8] = {0};
        for (int c = 0; c < 8; c++) {
            int h = heights[(c + offset) % 16];
            for (int r = 8 - h; r < 8; r++)
                fb[r] |= (0x80 >> c);
        }
        show_fb(fb, 60);
    }
}

/* --- Effect 14: Snake traversing the grid in boustrophedon order --- */
static void effect_snake(void) {
    uint8_t path_r[64], path_c[64];
    int idx = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            path_r[idx] = r;
            path_c[idx] = (r % 2 == 0) ? c : 7 - c;
            idx++;
        }
    }
    const int snake_len = 5;
    for (int head = 0; head < 64 + snake_len; head++) {
        uint8_t fb[8] = {0};
        for (int s = 0; s < snake_len; s++) {
            int pos = head - s;
            if (pos >= 0 && pos < 64)
                fb[path_r[pos]] |= (0x80 >> path_c[pos]);
        }
        show_fb(fb, 45);
    }
}

/* --- Effect 15: Starburst radiating from center --- */
static void effect_starburst(void) {
    static const uint8_t bursts[7][8] = {
        {0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00}, /* center dot */
        {0x18, 0x18, 0x18, 0xFF, 0xFF, 0x18, 0x18, 0x18}, /* cross */
        {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}, /* diagonals */
        {0x99, 0x5A, 0x3C, 0xFF, 0xFF, 0x3C, 0x5A, 0x99}, /* combined */
        {0xBD, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0xBD}, /* nearly full */
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* full flash */
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* blank */
    };
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 7; i++)
            show_fb(bursts[i], 90);
        for (int i = 5; i >= 0; i--)
            show_fb(bursts[i], 90);
    }
}

/* --- Effect 16: Random letters A-Z / a-z ---
 * Each character is rendered as an 8x8 bitmap using a 5x7 font scaled to fit.
 * 52 glyphs total (A-Z then a-z). Each glyph is 8 bytes (one per row). */

/* 5-wide glyphs packed into 8 columns: bits 7..3 = glyph, bits 2..0 = unused (zero-padded right). */
static const uint8_t font_letters[52][8] = {
    /* A */ {0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00},
    /* B */ {0xF8, 0xCC, 0xCC, 0xF8, 0xCC, 0xCC, 0xF8, 0x00},
    /* C */ {0x78, 0xCC, 0xC0, 0xC0, 0xC0, 0xCC, 0x78, 0x00},
    /* D */ {0xF0, 0xD8, 0xCC, 0xCC, 0xCC, 0xD8, 0xF0, 0x00},
    /* E */ {0xFC, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFC, 0x00},
    /* F */ {0xFC, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00},
    /* G */ {0x78, 0xCC, 0xC0, 0xDC, 0xCC, 0xCC, 0x78, 0x00},
    /* H */ {0xCC, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0xCC, 0x00},
    /* I */ {0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00},
    /* J */ {0x1C, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00},
    /* K */ {0xCC, 0xD8, 0xF0, 0xE0, 0xF0, 0xD8, 0xCC, 0x00},
    /* L */ {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFC, 0x00},
    /* M */ {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00},
    /* N */ {0xCC, 0xEC, 0xFC, 0xDC, 0xCC, 0xCC, 0xCC, 0x00},
    /* O */ {0x78, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x00},
    /* P */ {0xF8, 0xCC, 0xCC, 0xF8, 0xC0, 0xC0, 0xC0, 0x00},
    /* Q */ {0x78, 0xCC, 0xCC, 0xCC, 0xDC, 0x78, 0x1C, 0x00},
    /* R */ {0xF8, 0xCC, 0xCC, 0xF8, 0xD8, 0xCC, 0xCC, 0x00},
    /* S */ {0x78, 0xCC, 0xC0, 0x78, 0x0C, 0xCC, 0x78, 0x00},
    /* T */ {0xFC, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00},
    /* U */ {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x00},
    /* V */ {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00},
    /* W */ {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},
    /* X */ {0xCC, 0xCC, 0x78, 0x30, 0x78, 0xCC, 0xCC, 0x00},
    /* Y */ {0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x30, 0x30, 0x00},
    /* Z */ {0xFC, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFC, 0x00},
    /* a */ {0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00},
    /* b */ {0xC0, 0xC0, 0xF8, 0xCC, 0xCC, 0xCC, 0xF8, 0x00},
    /* c */ {0x00, 0x00, 0x78, 0xCC, 0xC0, 0xCC, 0x78, 0x00},
    /* d */ {0x0C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x7C, 0x00},
    /* e */ {0x00, 0x00, 0x78, 0xCC, 0xFC, 0xC0, 0x78, 0x00},
    /* f */ {0x38, 0x60, 0x60, 0xF8, 0x60, 0x60, 0x60, 0x00},
    /* g */ {0x00, 0x00, 0x7C, 0xCC, 0xCC, 0x7C, 0x0C, 0x78},
    /* h */ {0xC0, 0xC0, 0xD8, 0xEC, 0xCC, 0xCC, 0xCC, 0x00},
    /* i */ {0x30, 0x00, 0x70, 0x30, 0x30, 0x30, 0x78, 0x00},
    /* j */ {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78},
    /* k */ {0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0x00},
    /* l */ {0x70, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00},
    /* m */ {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    /* n */ {0x00, 0x00, 0xD8, 0xEC, 0xCC, 0xCC, 0xCC, 0x00},
    /* o */ {0x00, 0x00, 0x78, 0xCC, 0xCC, 0xCC, 0x78, 0x00},
    /* p */ {0x00, 0x00, 0xF8, 0xCC, 0xCC, 0xF8, 0xC0, 0xC0},
    /* q */ {0x00, 0x00, 0x7C, 0xCC, 0xCC, 0x7C, 0x0C, 0x0C},
    /* r */ {0x00, 0x00, 0xD8, 0xEC, 0xC0, 0xC0, 0xC0, 0x00},
    /* s */ {0x00, 0x00, 0x78, 0xC0, 0x78, 0x0C, 0x78, 0x00},
    /* t */ {0x60, 0x60, 0xF8, 0x60, 0x60, 0x60, 0x38, 0x00},
    /* u */ {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00},
    /* v */ {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00},
    /* w */ {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00},
    /* x */ {0x00, 0x00, 0xCC, 0x78, 0x30, 0x78, 0xCC, 0x00},
    /* y */ {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0x78},
    /* z */ {0x00, 0x00, 0xFC, 0x18, 0x30, 0x60, 0xFC, 0x00},
};

static void effect_random_letters(void) {
    uint32_t rng = 0xABCD1234;
    for (int i = 0; i < 20; i++) {
        rng = rng * 1664525u + 1013904223u;
        uint8_t idx = (rng >> 16) % 52;
        /* Show letter */
        show_fb(font_letters[idx], 400);
        /* Brief blank between letters */
        max7219_clear();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

/* ---- Effect-number display ----
 * 5x7 digit glyphs (0-9) packed into 8 bytes each.
 * For single-digit numbers the glyph is centred.
 * For two-digit numbers (10-16) both glyphs are placed side-by-side (4 cols each). */

static const uint8_t font_digits[10][8] = {
    /* 0 */ {0x78, 0xCC, 0xDC, 0xEC, 0xCC, 0xCC, 0x78, 0x00},
    /* 1 */ {0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xFC, 0x00},
    /* 2 */ {0x78, 0xCC, 0x0C, 0x38, 0x60, 0xCC, 0xFC, 0x00},
    /* 3 */ {0x78, 0xCC, 0x0C, 0x38, 0x0C, 0xCC, 0x78, 0x00},
    /* 4 */ {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00},
    /* 5 */ {0xFC, 0xC0, 0xF8, 0x0C, 0x0C, 0xCC, 0x78, 0x00},
    /* 6 */ {0x38, 0x60, 0xC0, 0xF8, 0xCC, 0xCC, 0x78, 0x00},
    /* 7 */ {0xFC, 0xCC, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    /* 8 */ {0x78, 0xCC, 0xCC, 0x78, 0xCC, 0xCC, 0x78, 0x00},
    /* 9 */ {0x78, 0xCC, 0xCC, 0x7C, 0x0C, 0x18, 0x70, 0x00},
};

static void show_effect_number(int n) {
    uint8_t fb[8] = {0};
    if (n < 10) {
        /* Single digit: show full-width glyph centred (shift 1 col right) */
        for (int r = 0; r < 8; r++)
            fb[r] = font_digits[n][r] >> 1;
    } else {
        /* Two digits side-by-side: tens on left (cols 0-3), units on right (cols 4-7) */
        int tens = n / 10;
        int units = n % 10;
        for (int r = 0; r < 8; r++) {
            fb[r] = (font_digits[tens][r] >> 1) & 0xF0;   /* top nibble */
            fb[r] |= (font_digits[units][r] >> 5) & 0x0F; /* bottom nibble */
        }
    }
    show_fb(fb, 600);
    max7219_clear();
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* ---- Main effects task ---- */

static void max7219_effects_task(void *pvParameters) {
    while (1) {
        show_effect_number(1);
        effect_rings();
        show_effect_number(2);
        effect_checkerboard();
        show_effect_number(3);
        effect_propeller();
        show_effect_number(4);
        effect_wipe();
        show_effect_number(5);
        effect_spiral();
        show_effect_number(6);
        effect_bounce();
        show_effect_number(7);
        effect_cross_rotate();
        show_effect_number(8);
        effect_rain();
        show_effect_number(9);
        effect_sparkle();
        show_effect_number(10);
        effect_heartbeat();
        show_effect_number(11);
        effect_scanner();
        show_effect_number(12);
        effect_fire();
        show_effect_number(13);
        effect_wave();
        show_effect_number(14);
        effect_snake();
        show_effect_number(15);
        effect_starburst();
        show_effect_number(16);
        effect_random_letters();
        max7219_clear();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void max7219_init(void) {
    ESP_LOGI(TAG, "Initializing MAX7219 (CS=%d, CLK=%d, DIN=%d)", MAX7219_CS_PIN, MAX7219_CLK_PIN, MAX7219_DIN_PIN);

    /* Configure GPIO pins */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MAX7219_CS_PIN) | (1ULL << MAX7219_CLK_PIN) | (1ULL << MAX7219_DIN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(MAX7219_CS_PIN, 1);
    gpio_set_level(MAX7219_CLK_PIN, 0);

    /* MAX7219 initialization sequence */
    max7219_write(REG_TEST, 0x00);        /* Normal operation (not test) */
    max7219_write(REG_SCAN_LIMIT, 0x07);  /* Display digits 0-7 */
    max7219_write(REG_DECODE_MODE, 0x00); /* No BCD decode — raw segments */
    max7219_write(REG_INTENSITY, 0x08);   /* Medium brightness */
    max7219_write(REG_SHUTDOWN, 0x01);    /* Normal operation (not shutdown) */

    max7219_clear();

    xTaskCreate(max7219_effects_task, "max7219_effects", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "MAX7219 initialized, effects running");
}
