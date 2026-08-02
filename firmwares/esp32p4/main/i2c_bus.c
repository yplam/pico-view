// Shared expansion I2C master bus. See i2c_bus.h.
#include "i2c_bus.h"

#include "sdkconfig.h"
#include "esp_check.h"

static const char *TAG = "pv_i2c";

esp_err_t pv_i2c_bus_get(i2c_master_bus_handle_t *out) {
#if CONFIG_PV_TOUCH_ENABLE || CONFIG_PV_HAPTICS_ENABLE
    static i2c_master_bus_handle_t s_bus;
    if (!s_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = CONFIG_PV_TOUCH_I2C_PORT,
            .sda_io_num = CONFIG_PV_PIN_TOUCH_SDA,
            .scl_io_num = CONFIG_PV_PIN_TOUCH_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");
        ESP_LOGI(TAG, "expansion I2C bus up (port %d, SDA %d, SCL %d)",
                 CONFIG_PV_TOUCH_I2C_PORT, CONFIG_PV_PIN_TOUCH_SDA,
                 CONFIG_PV_PIN_TOUCH_SCL);
    }
    *out = s_bus;
    return ESP_OK;
#else
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
