// DRV2605L haptic driver. See haptics.h.
#include "haptics.h"

#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bus.h"

#if CONFIG_PV_HAPTICS_ENABLE

static const char *TAG = "pv_haptics";

// --- DRV2605L register map (datasheet section 7.6) ---------------------------
#define DRV_REG_MODE      0x01
#define DRV_REG_LIBRARY   0x03
#define DRV_REG_WAVESEQ1  0x04  // 0x04..0x0B = sequencer slots 1..8; 0 ends it
#define DRV_REG_WAVESEQ2  0x05
#define DRV_REG_GO        0x0C
#define DRV_REG_RATED_V   0x16
#define DRV_REG_OD_CLAMP  0x17
#define DRV_REG_FEEDBACK  0x1A
#define DRV_REG_CONTROL3  0x1D

#define DRV_MODE_INTTRIG  0x00  // internal trigger: playback starts on GO
#define DRV_FEEDBACK_LRA  0x80  // FEEDBACK_CTRL bit7 N_ERM_LRA (1 = LRA)
#define DRV_CONTROL3_LRA_OPEN_LOOP 0x01  // CONTROL3 bit0

#define DRV_I2C_TIMEOUT_MS 20

// CONFIG_PV_HAPTICS_LRA is an unset-able Kconfig bool, so the symbol is undefined
// (not 0) for an ERM. Fold it to a plain 0/1 usable outside the preprocessor.
#if CONFIG_PV_HAPTICS_LRA
#define PV_HAPTICS_LRA_FLAG 1
#else
#define PV_HAPTICS_LRA_FLAG 0
#endif

static i2c_master_dev_handle_t s_dev;
static uint8_t s_library;  // ROM library currently selected on the chip

static esp_err_t reg_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(DRV_I2C_TIMEOUT_MS));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1,
                                       pdMS_TO_TICKS(DRV_I2C_TIMEOUT_MS));
}

// Read-modify-write: set the bits in `set`, clear the bits in `clear`.
static esp_err_t reg_update(uint8_t reg, uint8_t set, uint8_t clear) {
    uint8_t v;
    ESP_RETURN_ON_ERROR(reg_read(reg, &v), TAG, "read 0x%02x", reg);
    v = (uint8_t)((v & ~clear) | set);
    return reg_write(reg, v);
}
#endif  // CONFIG_PV_HAPTICS_ENABLE

esp_err_t pv_haptics_configure(void) {
#if CONFIG_PV_HAPTICS_ENABLE
    if (!s_dev) {
        i2c_master_bus_handle_t bus;
        ESP_RETURN_ON_ERROR(pv_i2c_bus_get(&bus), TAG, "shared i2c bus");
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CONFIG_PV_HAPTICS_I2C_ADDR,
            .scl_speed_hz = CONFIG_PV_HAPTICS_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "i2c dev");
    }

    // Wake from standby into internal-trigger mode (power-up default is standby).
    ESP_RETURN_ON_ERROR(reg_write(DRV_REG_MODE, DRV_MODE_INTTRIG), TAG, "mode");

    s_library = CONFIG_PV_HAPTICS_LIBRARY;
    ESP_RETURN_ON_ERROR(reg_write(DRV_REG_LIBRARY, s_library), TAG, "library");

#if CONFIG_PV_HAPTICS_LRA
    // Select LRA feedback and run the sequencer open-loop (no auto-calibration:
    // that needs this actuator's measured parameters — add later if desired).
    ESP_RETURN_ON_ERROR(reg_update(DRV_REG_FEEDBACK, DRV_FEEDBACK_LRA, 0), TAG, "feedback");
    ESP_RETURN_ON_ERROR(reg_update(DRV_REG_CONTROL3, DRV_CONTROL3_LRA_OPEN_LOOP, 0),
                        TAG, "control3");
#else
    ESP_RETURN_ON_ERROR(reg_update(DRV_REG_FEEDBACK, 0, DRV_FEEDBACK_LRA), TAG, "feedback");
#endif

#if CONFIG_PV_HAPTICS_RATED_MV > 0
    // RATED_VOLTAGE = round(rated_mv / 21.18). See datasheet eq. for open loop.
    ESP_RETURN_ON_ERROR(
        reg_write(DRV_REG_RATED_V, (uint8_t)((CONFIG_PV_HAPTICS_RATED_MV * 100 + 1059) / 2118)),
        TAG, "rated");
#endif
#if CONFIG_PV_HAPTICS_CLAMP_MV > 0
    // OD_CLAMP = round(clamp_mv / 21.32) for open-loop LRA.
    ESP_RETURN_ON_ERROR(
        reg_write(DRV_REG_OD_CLAMP, (uint8_t)((CONFIG_PV_HAPTICS_CLAMP_MV * 100 + 1066) / 2132)),
        TAG, "clamp");
#endif

    ESP_LOGI(TAG, "DRV2605L ready: addr=0x%02x lib=%u lra=%d",
             CONFIG_PV_HAPTICS_I2C_ADDR, s_library, PV_HAPTICS_LRA_FLAG);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

bool pv_haptics_available(void) {
#if CONFIG_PV_HAPTICS_ENABLE
    return true;
#else
    return false;
#endif
}

void pv_haptics_play(uint8_t effect, uint8_t library) {
#if CONFIG_PV_HAPTICS_ENABLE
    if (!s_dev) return;
    if (effect == 0 || effect > 127) {
        ESP_LOGW(TAG, "ignoring out-of-range effect %u", effect);
        return;
    }
    esp_err_t err = ESP_OK;
    // Switch the ROM library only when the host asks for a different one.
    if (library != 0 && library <= 7 && library != s_library) {
        if ((err = reg_write(DRV_REG_LIBRARY, library)) == ESP_OK) s_library = library;
    }
    if (err == ESP_OK) err = reg_write(DRV_REG_MODE, DRV_MODE_INTTRIG);
    if (err == ESP_OK) err = reg_write(DRV_REG_WAVESEQ1, effect & 0x7F);
    if (err == ESP_OK) err = reg_write(DRV_REG_WAVESEQ2, 0);  // terminate the sequence
    if (err == ESP_OK) err = reg_write(DRV_REG_GO, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "play effect %u failed: %s", effect, esp_err_to_name(err));
    }
#else
    (void)effect; (void)library;
#endif
}

void pv_haptics_stop(void) {
#if CONFIG_PV_HAPTICS_ENABLE
    if (!s_dev) return;
    esp_err_t err = reg_write(DRV_REG_GO, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "stop failed: %s", esp_err_to_name(err));
#endif
}
