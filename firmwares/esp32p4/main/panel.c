// QSPI panel backend. See panel.h. The ST77916 init sequence is ported verbatim
// from the host engine's driver.rs (St77916) -- it is the W180TE010I 360x360
// reference power-on code -- and the windowing from lcd.rs::write_window.
//
// The panel is driven over QSPI (4 data lines, no D/C line). esp_lcd's quad_mode
// puts the command byte on a single line and the pixel payload on all four; the
// ST77916 QSPI framing prepends a 1-byte opcode to each 8-bit command, carried in
// the high byte of a 32-bit command word: 0x02 for a register/parameter write,
// 0x32 for a RAMWR color write (see qcmd()/qcolor() below).
#include "panel.h"

#include <stdatomic.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "pv_panel";

// MIPI DBI command set (shared by ST7789 / ST77916).
#define CMD_CASET  0x2A
#define CMD_RASET  0x2B
#define CMD_RAMWR  0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A
#define CMD_INVOFF 0x20
#define CMD_INVON  0x21
#define CMD_SLPOUT 0x11
#define CMD_DISPON 0x29

// ST77916 QSPI command opcodes, carried in the top byte of the 32-bit command
// word (lcd_cmd_bits = 32). 0x02 = register/parameter write, 0x32 = color write.
#define QSPI_OPCODE_WRITE_CMD   0x02u
#define QSPI_OPCODE_WRITE_COLOR 0x32u
#define QSPI_CMD(op, c)   (((uint32_t)(op) << 24) | (((uint32_t)(c) & 0xFF) << 8))

// One init-sequence entry: a command plus 0..N parameter bytes.
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
} lcd_init_cmd_t;

// ST77916 vendor power-on sequence (transcribed from driver.rs ST77916_INIT;
// MADCTL/COLMOD/INVxx/SLPOUT/DISPON are emitted separately so rotation+invert
// are honoured and GRAM is cleared before the display turns on).
static const lcd_init_cmd_t st77916_init[] = {
    {0xF0,1,{0x28}},{0xF2,1,{0x28}},{0x73,1,{0xF0}},{0x7C,1,{0xD1}},
    {0x83,1,{0xE0}},{0x84,1,{0x61}},{0xF2,1,{0x82}},{0xF0,1,{0x00}},
    {0xF0,1,{0x01}},{0xF1,1,{0x01}},{0xB0,1,{0x5E}},{0xB1,1,{0x55}},
    {0xB2,1,{0x24}},{0xB3,1,{0x01}},{0xB4,1,{0x87}},{0xB5,1,{0x44}},
    {0xB6,1,{0x8B}},{0xB7,1,{0x40}},{0xB8,1,{0x86}},{0xB9,1,{0x15}},
    {0xBA,1,{0x00}},{0xBB,1,{0x08}},{0xBC,1,{0x08}},{0xBD,1,{0x00}},
    {0xBE,1,{0x00}},{0xBF,1,{0x07}},{0xC0,1,{0x80}},{0xC1,1,{0x10}},
    {0xC2,1,{0x37}},{0xC3,1,{0x80}},{0xC4,1,{0x10}},{0xC5,1,{0x37}},
    {0xC6,1,{0xA9}},{0xC7,1,{0x41}},{0xC8,1,{0x01}},{0xC9,1,{0xA9}},
    {0xCA,1,{0x41}},{0xCB,1,{0x01}},{0xCC,1,{0x7F}},{0xCD,1,{0x7F}},
    {0xCE,1,{0xFF}},{0xD0,1,{0x91}},{0xD1,1,{0x68}},{0xD2,1,{0x68}},
    {0xF5,2,{0x00,0xA5}},{0xDD,1,{0x40}},{0xDE,1,{0x40}},{0xF1,1,{0x10}},
    {0xF0,1,{0x00}},{0xF0,1,{0x02}},
    {0xE0,14,{0xF0,0x10,0x18,0x0D,0x0C,0x38,0x3E,0x44,0x51,0x39,0x15,0x15,0x30,0x34}},
    {0xE1,14,{0xF0,0x0F,0x17,0x0D,0x0B,0x07,0x3E,0x33,0x51,0x39,0x15,0x15,0x30,0x34}},
    {0xF0,1,{0x10}},{0xF3,1,{0x10}},{0xE0,1,{0x08}},{0xE1,1,{0x00}},
    {0xE2,1,{0x00}},{0xE3,1,{0x00}},{0xE4,1,{0xE0}},{0xE5,1,{0x06}},
    {0xE6,1,{0x21}},{0xE7,1,{0x03}},{0xE8,1,{0x05}},{0xE9,1,{0x02}},
    {0xEA,1,{0xE9}},{0xEB,1,{0x00}},{0xEC,1,{0x00}},{0xED,1,{0x14}},
    {0xEE,1,{0xFF}},{0xEF,1,{0x00}},{0xF8,1,{0xFF}},{0xF9,1,{0x00}},
    {0xFA,1,{0x00}},{0xFB,1,{0x30}},{0xFC,1,{0x00}},{0xFD,1,{0x00}},
    {0xFE,1,{0x00}},{0xFF,1,{0x00}},{0x60,1,{0x40}},{0x61,1,{0x05}},
    {0x62,1,{0x00}},{0x63,1,{0x42}},{0x64,1,{0xDA}},{0x65,1,{0x00}},
    {0x66,1,{0x00}},{0x67,1,{0x00}},{0x68,1,{0x00}},{0x69,1,{0x00}},
    {0x6A,1,{0x00}},{0x6B,1,{0x00}},{0x70,1,{0x40}},{0x71,1,{0x04}},
    {0x72,1,{0x00}},{0x73,1,{0x42}},{0x74,1,{0xD9}},{0x75,1,{0x00}},
    {0x76,1,{0x00}},{0x77,1,{0x00}},{0x78,1,{0x00}},{0x79,1,{0x00}},
    {0x7A,1,{0x00}},{0x7B,1,{0x00}},{0x80,1,{0x48}},{0x81,1,{0x00}},
    {0x82,1,{0x07}},{0x83,1,{0x02}},{0x84,1,{0xD7}},{0x85,1,{0x04}},
    {0x86,1,{0x00}},{0x87,1,{0x00}},{0x88,1,{0x48}},{0x89,1,{0x00}},
    {0x8A,1,{0x09}},{0x8B,1,{0x02}},{0x8C,1,{0xD9}},{0x8D,1,{0x04}},
    {0x8E,1,{0x00}},{0x8F,1,{0x00}},{0x90,1,{0x48}},{0x91,1,{0x00}},
    {0x92,1,{0x0B}},{0x93,1,{0x02}},{0x94,1,{0xDB}},{0x95,1,{0x04}},
    {0x96,1,{0x00}},{0x97,1,{0x00}},{0x98,1,{0x48}},{0x99,1,{0x00}},
    {0x9A,1,{0x0D}},{0x9B,1,{0x02}},{0x9C,1,{0xDD}},{0x9D,1,{0x04}},
    {0x9E,1,{0x00}},{0x9F,1,{0x00}},{0xA0,1,{0x48}},{0xA1,1,{0x00}},
    {0xA2,1,{0x06}},{0xA3,1,{0x02}},{0xA4,1,{0xD6}},{0xA5,1,{0x04}},
    {0xA6,1,{0x00}},{0xA7,1,{0x00}},{0xA8,1,{0x48}},{0xA9,1,{0x00}},
    {0xAA,1,{0x08}},{0xAB,1,{0x02}},{0xAC,1,{0xD8}},{0xAD,1,{0x04}},
    {0xAE,1,{0x00}},{0xAF,1,{0x00}},{0xB0,1,{0x48}},{0xB1,1,{0x00}},
    {0xB2,1,{0x0A}},{0xB3,1,{0x02}},{0xB4,1,{0xDA}},{0xB5,1,{0x04}},
    {0xB6,1,{0x00}},{0xB7,1,{0x00}},{0xB8,1,{0x48}},{0xB9,1,{0x00}},
    {0xBA,1,{0x0C}},{0xBB,1,{0x02}},{0xBC,1,{0xDC}},{0xBD,1,{0x04}},
    {0xBE,1,{0x00}},{0xBF,1,{0x00}},{0xC0,1,{0x10}},{0xC1,1,{0x47}},
    {0xC2,1,{0x56}},{0xC3,1,{0x65}},{0xC4,1,{0x74}},{0xC5,1,{0x88}},
    {0xC6,1,{0x99}},{0xC7,1,{0x01}},{0xC8,1,{0xBB}},{0xC9,1,{0xAA}},
    {0xD0,1,{0x10}},{0xD1,1,{0x47}},{0xD2,1,{0x56}},{0xD3,1,{0x65}},
    {0xD4,1,{0x74}},{0xD5,1,{0x88}},{0xD6,1,{0x99}},{0xD7,1,{0x01}},
    {0xD8,1,{0xBB}},{0xD9,1,{0xAA}},{0xF3,1,{0x01}},{0xF0,1,{0x00}},
};

// ---------------------------------------------------------------------------
// Panel state
// ---------------------------------------------------------------------------
static esp_lcd_panel_io_handle_t s_io;
// Counting semaphore: each finished color transfer posts one token from the ISR;
// pv_panel_wait_one() consumes one to recycle the buffer that fed it. Counting
// (not binary) so several blits can be in flight without losing completions.
static SemaphoreHandle_t s_trans_done;
static bool s_bus_inited;
static bool s_bl_ready;  // backlight LEDC configured (set_backlight/set_brightness are live)
static spi_host_device_t s_host;
static uint16_t s_width, s_height, s_x_off, s_y_off;
// Latched true when a color completion times out (pv_panel_wait_one): the QSPI
// pipeline is stuck on a phantom in-flight transfer, and every further esp_lcd
// call risks blocking forever behind it (tx_param and io_del both drain queued
// color transactions with portMAX_DELAY). write_window refuses new work while
// set; rx_task polls pv_panel_faulted() and reboots to put the SPI peripheral
// back into a known state. Written from the panel task, read from rx_task --
// hence atomic. Cleared only by a successful pv_panel_configure (in practice:
// after the recovery reboot).
static atomic_bool s_faulted;

// Depth of the esp_lcd transaction queue; also the cap on outstanding color
// completion tokens the counting semaphore can hold.
#define PV_TRANS_QUEUE_DEPTH 10

// Upper bound on how long pv_panel_wait_one() blocks for a color completion. A
// full-frame RAMWR drains in ~tens of ms even at a conservative QSPI clock, so
// exceeding this means a completion was genuinely lost, not merely slow.
#define PV_PANEL_WAIT_TIMEOUT_MS 1000

uint16_t pv_panel_width(void) { return s_width; }
uint16_t pv_panel_height(void) { return s_height; }

// SPI color-DMA completion: posted from the esp_lcd ISR.
static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *e, void *ctx) {
    (void)io; (void)e; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &hp);
    return hp == pdTRUE;
}

static inline void be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }

// MADCTL scan-order byte (matches driver.rs::madctl).
static uint8_t madctl_for(uint16_t rotation) {
    switch (rotation) {
        case 90:  return 0x60;  // MV | MX
        case 180: return 0xC0;  // MY | MX
        case 270: return 0xA0;  // MV | MY
        default:  return 0x00;
    }
}

// One register/parameter write, QSPI-framed (opcode 0x02, command on a single
// data line, params on a single data line).
static void cmd(uint8_t c, const uint8_t *data, size_t len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(
        s_io, QSPI_CMD(QSPI_OPCODE_WRITE_CMD, c), data, len));
}

static void run_init_table(const lcd_init_cmd_t *t, size_t n) {
    for (size_t i = 0; i < n; i++) {
        cmd(t[i].cmd, t[i].data, t[i].len);
    }
}

// Window `rect` (offset into controller RAM) + RAMWR + pixels, queued async. The
// CASET/RASET params block briefly (they serialise behind any prior color in the
// SPI queue); the RAMWR pixel DMA is started and left running -- its completion
// is reaped later by pv_panel_wait_one().
static esp_err_t write_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              const uint8_t *pixels, size_t len) {
    // A faulted pipeline must not re-enter esp_lcd: the CASET below goes through
    // tx_param, which waits portMAX_DELAY on every queued color transaction --
    // including the stuck one that raised the fault.
    if (atomic_load(&s_faulted)) return ESP_ERR_INVALID_STATE;
    uint8_t caset[4], raset[4];
    be16(&caset[0], s_x_off + x);
    be16(&caset[2], s_x_off + x + w - 1);
    be16(&raset[0], s_y_off + y);
    be16(&raset[2], s_y_off + y + h - 1);
    cmd(CMD_CASET, caset, 4);
    cmd(CMD_RASET, raset, 4);
    // RAMWR color write: opcode 0x32, pixel payload streamed over all 4 data lines.
    return esp_lcd_panel_io_tx_color(
        s_io, QSPI_CMD(QSPI_OPCODE_WRITE_COLOR, CMD_RAMWR), pixels, len);
}

esp_err_t pv_panel_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *pixels, size_t len) {
    if (!s_io) return ESP_ERR_INVALID_STATE;
    return write_window(x, y, w, h, pixels, len);
}

bool pv_panel_wait_one(void) {
    if (!s_trans_done) return false;
    // Bounded, not portMAX_DELAY: a lost color completion (e.g. an SPI/DMA
    // glitch that never fires on_trans_done) must not pin the caller forever.
    if (xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(PV_PANEL_WAIT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "color completion timed out after %ums; panel stalled",
                 (unsigned)PV_PANEL_WAIT_TIMEOUT_MS);
        // Latch here, at the one place a stall is ever detected, so no caller can
        // forget to. write_window then refuses work and rx_task reboots.
        atomic_store(&s_faulted, true);
        return false;
    }
    return true;
}

bool pv_panel_faulted(void) {
    return atomic_load(&s_faulted);
}

// ---------------------------------------------------------------------------
// Backlight (LEDC PWM). Two independent inputs: `s_brightness` is the host-set
// 0..255 level (SetParam), `s_bl_on` is the present/suspend gate. Both feed
// bl_apply(), so a brightness change mid-gate takes effect immediately and one
// set while gated-off is remembered for the next on.
// ---------------------------------------------------------------------------
#if CONFIG_PV_PIN_LCD_BL >= 0
#define PV_BL_LEDC_MODE     LEDC_LOW_SPEED_MODE  // ESP32-P4 has no high-speed mode
#define PV_BL_LEDC_TIMER    LEDC_TIMER_0
#define PV_BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define PV_BL_DUTY_MAX      ((1u << CONFIG_PV_LCD_BL_PWM_RES_BITS) - 1u)

static uint8_t s_brightness = 255;  // host-set level (survives reconfigure)
static bool    s_bl_on;             // gate: is the light meant to be lit now

// Push the current (gate, brightness) to the LEDC channel. Active-low panels
// (BL_ON_LEVEL == 0) get an inverted duty so 0..255 always maps dark..full.
static void bl_apply(void) {
    if (!s_bl_ready) return;
    uint32_t level = s_bl_on ? s_brightness : 0;
    uint32_t duty = (level * PV_BL_DUTY_MAX + 127) / 255;  // rounded scale
#if CONFIG_PV_LCD_BL_ON_LEVEL == 0
    duty = PV_BL_DUTY_MAX - duty;
#endif
    ledc_set_duty(PV_BL_LEDC_MODE, PV_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(PV_BL_LEDC_MODE, PV_BL_LEDC_CHANNEL);
}
#endif

void pv_panel_set_backlight(bool on) {
#if CONFIG_PV_PIN_LCD_BL >= 0
    s_bl_on = on;
    bl_apply();
#else
    (void)on;
#endif
}

void pv_panel_set_brightness(uint8_t brightness) {
#if CONFIG_PV_PIN_LCD_BL >= 0
    s_brightness = brightness;
    bl_apply();  // immediate when the gate is on; remembered otherwise
#else
    (void)brightness;
#endif
}

uint8_t pv_panel_get_brightness(void) {
#if CONFIG_PV_PIN_LCD_BL >= 0
    return s_brightness;
#else
    return 255;
#endif
}

// Reset pulse on the RST GPIO: high `h1`ms, low `lo`ms, high `h2`ms.
static void reset_pulse(uint32_t h1, uint32_t lo, uint32_t h2) {
#if CONFIG_PV_PIN_LCD_RST >= 0
    gpio_set_level(CONFIG_PV_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(h1 ? h1 : 1));
    gpio_set_level(CONFIG_PV_PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(lo));
    gpio_set_level(CONFIG_PV_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(h2));
#else
    (void)h1; (void)lo; (void)h2;
#endif
}

// Returns false only if a queued clear stalled: the caller must not proceed to
// further blocking esp_lcd calls, which would wedge draining the stuck transfer.
// A skipped clear (no PSRAM) and a successful clear both return true.
static bool clear_gram(void) {
    size_t len = (size_t)s_width * s_height * 2;
    // Cache-line aligned start AND size: this buffer feeds the SPI DMA from
    // PSRAM, and an unaligned edge at either end pushes the driver through
    // edge-case cache-sync paths.
    uint8_t *zero = heap_caps_aligned_alloc(PV_DMA_ALIGN, PV_DMA_SIZE(len), MALLOC_CAP_SPIRAM);
    if (!zero) {
        ESP_LOGW(TAG, "no PSRAM for GRAM clear (%u bytes); skipping", (unsigned)len);
        return true;  // nothing queued; the pipeline is not stalled
    }
    memset(zero, 0, len);
    // Synchronous: queue the clear and reap its completion so the semaphore
    // stays balanced before streaming starts.
    bool ok;
    if (write_window(0, 0, s_width, s_height, zero, len) == ESP_OK) {
        ok = pv_panel_wait_one();  // false => a completion was lost; pipeline stalled
    } else {
        ok = false;  // the clear didn't queue: don't proceed as if it drained
    }
    heap_caps_free(zero);
    return ok;
}

void pv_panel_clear(void) {
    if (!s_io) return;  // not configured yet: nothing to wipe
    // Ignore the result: on a stall pv_panel_wait_one already latched the fault,
    // and rx_task polls pv_panel_faulted() to reboot.
    (void)clear_gram();
}

static void teardown(void) {
    if (s_io) { esp_lcd_panel_io_del(s_io); s_io = NULL; }
    if (s_bus_inited) { spi_bus_free(s_host); s_bus_inited = false; }
}

esp_err_t pv_panel_configure(const pv_config_t *cfg) {
    teardown();

    s_width  = cfg->width;
    s_height = cfg->height;
    s_x_off  = cfg->x_offset;
    s_y_off  = cfg->y_offset;
    s_host   = (spi_host_device_t)CONFIG_PV_LCD_SPI_HOST;

    if (!s_trans_done) {
        s_trans_done = xSemaphoreCreateCounting(PV_TRANS_QUEUE_DEPTH, 0);
        if (!s_trans_done) return ESP_ERR_NO_MEM;
    } else {
        // Re-CONFIG: drop any completion token left over from the previous
        // pipeline (e.g. a transfer that completed after its wait timed out) so
        // the accounting restarts at zero.
        while (xSemaphoreTake(s_trans_done, 0) == pdTRUE) {}
    }

    // RST GPIO (esp_lcd owns CS + DC; the backlight is an LEDC pin, below).
#if CONFIG_PV_PIN_LCD_RST >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_PV_PIN_LCD_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
#endif

    // Backlight: LEDC PWM so the host can dim it (SetParam brightness). Timer +
    // channel config are idempotent, so re-CONFIG is safe; s_brightness is left
    // untouched so a host-set level survives a reconfigure.
#if CONFIG_PV_PIN_LCD_BL >= 0
    ledc_timer_config_t bl_timer = {
        .speed_mode      = PV_BL_LEDC_MODE,
        .timer_num       = PV_BL_LEDC_TIMER,
        .duty_resolution = CONFIG_PV_LCD_BL_PWM_RES_BITS,
        .freq_hz         = CONFIG_PV_LCD_BL_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));
    ledc_channel_config_t bl_ch = {
        .gpio_num   = CONFIG_PV_PIN_LCD_BL,
        .speed_mode = PV_BL_LEDC_MODE,
        .channel    = PV_BL_LEDC_CHANNEL,
        .timer_sel  = PV_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_ch));
    s_bl_ready = true;
    s_bl_on = false;
    bl_apply();  // drive the OFF electrical level during init (honours active-low)
#endif

    if (cfg->model != PV_PANEL_ST77916) {
        ESP_LOGW(TAG, "model %u not supported by this QSPI firmware; "
                 "driving as ST77916", cfg->model);
    }

    // QSPI bus: SCLK + 4 data lines (D0..D3), no MOSI/MISO, no D/C line.
    spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_PV_PIN_LCD_SCLK,
        .data0_io_num = CONFIG_PV_PIN_LCD_D0,
        .data1_io_num = CONFIG_PV_PIN_LCD_D1,
        .data2_io_num = CONFIG_PV_PIN_LCD_D2,
        .data3_io_num = CONFIG_PV_PIN_LCD_D3,
        .max_transfer_sz = (int)((size_t)s_width * s_height * 2),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(s_host, &bus, SPI_DMA_CH_AUTO));
    s_bus_inited = true;

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = CONFIG_PV_PIN_LCD_CS,
        .dc_gpio_num = -1,                  // QSPI: command opcode replaces D/C
        .spi_mode = 0,
        .pclk_hz = CONFIG_PV_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 32,                 // 8-bit cmd carried in a 32-bit opcode word
        .lcd_param_bits = 8,
        .flags = { .quad_mode = true },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)s_host, &io_cfg, &s_io));

    // --- Power-on sequence (per driver.rs, W180TE010I reference init) ---
    uint8_t madctl = madctl_for(cfg->rotation);
    reset_pulse(1, 10, 120);
    cmd(CMD_MADCTL, &madctl, 1);
    uint8_t colmod = 0x55; cmd(CMD_COLMOD, &colmod, 1);
    run_init_table(st77916_init, sizeof(st77916_init) / sizeof(st77916_init[0]));

    cmd(cfg->invert ? CMD_INVON : CMD_INVOFF, NULL, 0);
    cmd(CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Black the GRAM before DISPON so no power-up garbage flashes. On a stall,
    // bail before DISPON: its tx_param would block forever draining the stuck
    // transfer. Returning early also skips the s_faulted clear at the end, so
    // pv_panel_task_faulted() trips and rx_task reboots cleanly.
    if (!clear_gram()) {
        ESP_LOGE(TAG, "GRAM clear stalled during configure; aborting init");
        return ESP_ERR_TIMEOUT;
    }

    cmd(CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Backlight stays off here; the panel task turns it on at the first presented
    // frame so the panel never shows the freshly-cleared GRAM flash.

    // A configured pipeline starts clean. (In practice a genuine fault is only
    // cleared by the recovery reboot -- callers gate on pv_panel_faulted() before
    // re-configuring, since teardown() on a stuck pipeline would block forever in
    // esp_lcd_panel_io_del.)
    atomic_store(&s_faulted, false);

    ESP_LOGI(TAG, "panel configured: model=%u %ux%u off=(%u,%u) rot=%u inv=%u",
             cfg->model, s_width, s_height, s_x_off, s_y_off, cfg->rotation, cfg->invert);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// OTA progress screen: a handful of fills, so it reuses the windowed color path
// with a banded scratch buffer rather than a full-frame allocation.
// ---------------------------------------------------------------------------

// Fill a clipped rectangle with one RGB565 colour (streamed big-endian, matching
// the blit pixel order). Banded so the scratch buffer stays ~16KB regardless of
// panel size.
static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!s_io || w == 0 || h == 0 || x >= s_width || y >= s_height) return;
    if (x + w > s_width)  w = s_width - x;
    if (y + h > s_height) h = s_height - y;

    uint16_t band = (uint16_t)(16384u / ((uint32_t)w * 2u));
    if (band == 0) band = 1;
    if (band > h) band = h;
    size_t buf_len = (size_t)w * band * 2;
    // Cache-line aligned for the same PSRAM-DMA reason as clear_gram. w*band*2
    // is rarely a whole number of lines, so the tail padding matters most here.
    uint8_t *buf = heap_caps_aligned_alloc(PV_DMA_ALIGN, PV_DMA_SIZE(buf_len), MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGW(TAG, "no PSRAM for %ux%u fill", (unsigned)w, (unsigned)h);
        return;
    }
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (size_t i = 0; i < buf_len; i += 2) { buf[i] = hi; buf[i + 1] = lo; }

    for (uint16_t row = 0; row < h; row += band) {
        uint16_t rows = (uint16_t)((h - row < band) ? (h - row) : band);
        if (write_window(x, y + row, w, rows, buf, (size_t)w * rows * 2) == ESP_OK) {
            pv_panel_wait_one();
        }
    }
    heap_caps_free(buf);
}

// Centred progress bar geometry for the current panel.
static void progress_bar_geom(uint16_t *bx, uint16_t *by, uint16_t *bw, uint16_t *bh) {
    uint16_t w = (uint16_t)((uint32_t)s_width * 4 / 5);
    uint16_t h = (uint16_t)(s_height / 12);
    if (h < 12) h = 12;
    *bw = w;
    *bh = h;
    *bx = (uint16_t)((s_width - w) / 2);
    *by = (uint16_t)((s_height - h) / 2);
}

#define PV_PROGRESS_BAR_INSET 2

void pv_panel_progress_begin(bool is_recovery) {
    if (!s_io) return;
    uint16_t bg = is_recovery ? 0x3000 /* dark red */ : 0x10A2 /* dark blue-gray */;
    fill_rect(0, 0, s_width, s_height, bg);
    uint16_t bx, by, bw, bh;
    progress_bar_geom(&bx, &by, &bw, &bh);
    fill_rect(bx, by, bw, bh, 0x4208 /* track gray */);
}

void pv_panel_progress_update(uint8_t pct) {
    if (!s_io) return;
    if (pct > 100) pct = 100;
    uint16_t bx, by, bw, bh;
    progress_bar_geom(&bx, &by, &bw, &bh);
    if (bw <= 2 * PV_PROGRESS_BAR_INSET || bh <= 2 * PV_PROGRESS_BAR_INSET) return;
    uint16_t iw = (uint16_t)(bw - 2 * PV_PROGRESS_BAR_INSET);
    uint16_t ih = (uint16_t)(bh - 2 * PV_PROGRESS_BAR_INSET);
    uint16_t fill = (uint16_t)((uint32_t)iw * pct / 100);
    if (fill) {
        fill_rect((uint16_t)(bx + PV_PROGRESS_BAR_INSET),
                  (uint16_t)(by + PV_PROGRESS_BAR_INSET),
                  fill, ih, 0x07E0 /* accent green */);
    }
}
