// CST816 touch poller. See touch.h. Mirrors host engine touch.rs.
#include "touch.h"

#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bus.h"

static const char *TAG = "pv_touch";

#if CONFIG_PV_TOUCH_ENABLE
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_enabled;
static bool s_down;
static uint16_t s_width, s_height;
static uint8_t s_flags;

static uint16_t max_coord(void) { return s_width > s_height ? s_width : s_height; }

static void touch_reset(void) {
#if CONFIG_PV_PIN_TOUCH_RST >= 0
    gpio_config_t io = { .pin_bit_mask = 1ULL << CONFIG_PV_PIN_TOUCH_RST,
                         .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(CONFIG_PV_PIN_TOUCH_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_PV_PIN_TOUCH_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_PV_PIN_TOUCH_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
#endif
}

// One CST816 read: Some -> finger down at (x,y); false -> no finger / error.
static bool read_xy(uint16_t *x, uint16_t *y) {
    uint8_t reg = 0x01, r[6] = {0};
    if (i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r),
                                    pdMS_TO_TICKS(20)) != ESP_OK) {
        return false;
    }
    if ((r[1] & 0x0F) == 0) return false;  // no finger
    *x = (((uint16_t)(r[2] & 0x0F)) << 8) | r[3];
    *y = (((uint16_t)(r[4] & 0x0F)) << 8) | r[5];
    return true;
}

static void transform(uint16_t *x, uint16_t *y) {
    if (s_flags & PV_TOUCH_SWAP_XY) { uint16_t t = *x; *x = *y; *y = t; }
    if ((s_flags & PV_TOUCH_FLIP_X) && s_width > 0) {
        uint16_t v = *x < s_width ? *x : s_width - 1;
        *x = (s_width - 1) - v;
    }
    if ((s_flags & PV_TOUCH_FLIP_Y) && s_height > 0) {
        uint16_t v = *y < s_height ? *y : s_height - 1;
        *y = (s_height - 1) - v;
    }
}
#endif  // CONFIG_PV_TOUCH_ENABLE

esp_err_t pv_touch_configure(const pv_config_t *cfg) {
#if CONFIG_PV_TOUCH_ENABLE
    s_enabled = false;
    s_down = false;
    if (cfg->touch_addr == 0) {
        ESP_LOGI(TAG, "touch disabled by config");
        return ESP_OK;
    }
    s_width = cfg->width;
    s_height = cfg->height;
    s_flags = cfg->touch_flags;

    // The CST816 shares the expansion I2C bus with the DRV2605L (haptics.c), so
    // the master bus is owned by i2c_bus.c and created exactly once.
    ESP_RETURN_ON_ERROR(pv_i2c_bus_get(&s_bus), TAG, "i2c bus");
    if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->touch_addr,
        .scl_speed_hz = CONFIG_PV_TOUCH_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "i2c dev");

    touch_reset();
    s_enabled = true;
    ESP_LOGI(TAG, "touch ready: addr=0x%02x %ux%u flags=0x%02x",
             cfg->touch_addr, s_width, s_height, s_flags);
    return ESP_OK;
#else
    (void)cfg;
    return ESP_OK;
#endif
}

bool pv_touch_active(void) {
#if CONFIG_PV_TOUCH_ENABLE
    return s_down;
#else
    return false;
#endif
}

bool pv_touch_poll(pv_touch_t *out) {
#if CONFIG_PV_TOUCH_ENABLE
    if (!s_enabled) return false;
    uint16_t x, y;
    if (read_xy(&x, &y)) {
        // Bus-noise guard: a coordinate past the largest panel axis is bogus.
        if (x >= max_coord() || y >= max_coord()) return false;
        transform(&x, &y);
        // Clamp to the visible geometry: on a non-square panel the guard above
        // only bounds against the larger axis, and transform() only clamps the
        // axes it flips, so an off-axis reading could still escape the panel.
        if (s_width  && x >= s_width)  x = s_width - 1;
        if (s_height && y >= s_height) y = s_height - 1;
        out->phase = s_down ? PV_TOUCH_MOVE : PV_TOUCH_DOWN;
        out->pad = 0;
        out->x = x;
        out->y = y;
        s_down = true;
        return true;
    }
    if (s_down) {
        s_down = false;
        out->phase = PV_TOUCH_UP;  // no position on release; host reuses last
        out->pad = 0;
        out->x = 0;
        out->y = 0;
        return true;
    }
    return false;
#else
    (void)out;
    return false;
#endif
}
