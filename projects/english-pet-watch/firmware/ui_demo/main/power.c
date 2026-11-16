#include "power.h"

#include <stdint.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "epw-power";

#define AXP2101_ADDR            0x34
#define AXP2101_I2C_TIMEOUT_MS  100

#define AXP_REG_STATUS1         0x00
#define AXP_REG_STATUS2         0x01
#define AXP_REG_ADC_CH_CTRL     0x30
#define AXP_REG_BAT_VOLT_H      0x34
#define AXP_REG_BAT_VOLT_L      0x35
#define AXP_REG_BAT_PERCENT     0xA4

/* IRQ Enable 1 / Status 1：bit3 = PWRON 短按 */
#define AXP_REG_IRQ_ENABLE1     0x41
#define AXP_REG_IRQ_STATUS1     0x49
#define AXP_IRQ_PWRKEY_SHORT    0x08

static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, AXP2101_I2C_TIMEOUT_MS);
}

static esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), AXP2101_I2C_TIMEOUT_MS);
}

esp_err_t power_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        esp_err_t err = bsp_i2c_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
            return err;
        }
        bus = bsp_i2c_get_handle();
    }
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not found: %s", esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    /* 打开 BAT / VSYS / VBUS / TS 的 ADC 通道（bit1 保留关闭）。 */
    uint8_t ch = 0;
    if (reg_read(AXP_REG_ADC_CH_CTRL, &ch, 1) == ESP_OK) {
        reg_write(AXP_REG_ADC_CH_CTRL, (uint8_t)((ch & ~0x02) | 0x1D));
    }

    uint8_t id = 0;
    if (reg_read(0x03, &id, 1) == ESP_OK) {
        ESP_LOGI(TAG, "AXP2101 ready, reg0x03=0x%02X", id);
    }
    return ESP_OK;
}

int power_battery_mv(void)
{
    uint8_t v[2] = {0};
    if (reg_read(AXP_REG_BAT_VOLT_H, v, 2) != ESP_OK) {
        return 0;
    }
    return ((v[0] & 0x1F) << 8) | v[1];
}

static bool battery_connected(void)
{
    uint8_t s1 = 0;
    if (reg_read(AXP_REG_STATUS1, &s1, 1) != ESP_OK) {
        return false;
    }
    return (s1 & 0x08) != 0;
}

int power_battery_percent(void)
{
    if (!battery_connected()) {
        return -1;
    }

    uint8_t pct = 0;
    if (reg_read(AXP_REG_BAT_PERCENT, &pct, 1) == ESP_OK && pct <= 100) {
        return pct;
    }

    /* 电量计不可用时，用电压粗估（3.3V–4.2V）。 */
    int mv = power_battery_mv();
    if (mv < 3000) {
        return -1;
    }
    int p = (mv - 3300) * 100 / (4200 - 3300);
    if (p < 0) {
        p = 0;
    }
    if (p > 100) {
        p = 100;
    }
    return p;
}

bool power_is_charging(void)
{
    uint8_t s2 = 0;
    if (reg_read(AXP_REG_STATUS2, &s2, 1) != ESP_OK) {
        return false;
    }
    return ((s2 >> 5) & 0x03) == 0x01;
}

bool power_is_vbus(void)
{
    static bool have_logged;
    static bool last;
    uint8_t s1 = 0;
    if (reg_read(AXP_REG_STATUS1, &s1, 1) != ESP_OK) {
        return false;
    }
    bool vbus = (s1 & 0x20) != 0;
    if (!have_logged || vbus != last) {
        have_logged = true;
        last = vbus;
        ESP_LOGI(TAG, "STATUS1=0x%02X vbus=%d (mode=%s)", s1, (int)vbus,
                 vbus ? "charging" : "normal");
    }
    return vbus;
}

void power_pwrkey_irq_init(void)
{
    uint8_t en = 0;
    if (reg_read(AXP_REG_IRQ_ENABLE1, &en, 1) == ESP_OK &&
        (en & AXP_IRQ_PWRKEY_SHORT) == 0) {
        reg_write(AXP_REG_IRQ_ENABLE1, (uint8_t)(en | AXP_IRQ_PWRKEY_SHORT));
    }
    /* 清掉上电时可能残留的短按标志，避免一开机就弹菜单。 */
    reg_write(AXP_REG_IRQ_STATUS1, AXP_IRQ_PWRKEY_SHORT);
}

bool power_pwrkey_short_pressed(void)
{
    uint8_t st = 0;
    if (reg_read(AXP_REG_IRQ_STATUS1, &st, 1) != ESP_OK) {
        return false;
    }
    if ((st & AXP_IRQ_PWRKEY_SHORT) == 0) {
        return false;
    }
    reg_write(AXP_REG_IRQ_STATUS1, AXP_IRQ_PWRKEY_SHORT); /* RW1C */
    return true;
}
