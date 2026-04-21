#include "battery_monitor.h"

#include <stdio.h>
#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "battery_monitor";

/* LilyGo T5 4.7" (ESP32): battery ADC is on GPIO36 = ADC1_CHANNEL_0.
 * The board has a 100k/100k resistor divider so the ADC sees half the
 * actual battery voltage.  We multiply the calibrated reading by 2. */
#define BATT_ADC_CHANNEL   ADC_CHANNEL_0
#define BATT_ADC_ATTEN     ADC_ATTEN_DB_12   /* 0–3.9 V input range */
#define BATT_ADC_BITWIDTH  ADC_BITWIDTH_12
#define BATT_DIVIDER       2                 /* voltage divider ratio */

/* LiPo charge curve (approximate linear interpolation) */
#define BATT_FULL_MV  4200
#define BATT_EMPTY_MV 3000

static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_valid  = false;

esp_err_t battery_monitor_init(void)
{
    /* Configure ADC1 in oneshot mode */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATT_ADC_ATTEN,
        .bitwidth = BATT_ADC_BITWIDTH,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BATT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Try to create a calibration handle (curve-fitting preferred, else line-fitting) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BATT_ADC_CHANNEL,
        .atten    = BATT_ADC_ATTEN,
        .bitwidth = BATT_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_valid = true;
        ESP_LOGI(TAG, "ADC calibration: curve-fitting");
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!s_cali_valid) {
        adc_cali_line_fitting_config_t lf_cfg = {
            .unit_id  = ADC_UNIT_1,
            .atten    = BATT_ADC_ATTEN,
            .bitwidth = BATT_ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_line_fitting(&lf_cfg, &s_cali_handle);
        if (ret == ESP_OK) {
            s_cali_valid = true;
            ESP_LOGI(TAG, "ADC calibration: line-fitting");
        }
    }
#endif

    if (!s_cali_valid) {
        ESP_LOGW(TAG, "ADC calibration not available; raw readings will be used");
    }

    ESP_LOGI(TAG, "Battery monitor ready (GPIO36, BATT_ADC_ATTEN=DB_12, divider=%d)", BATT_DIVIDER);
    return ESP_OK;
}

int battery_monitor_read_mv(void)
{
    if (!s_adc_handle) {
        return 0;
    }

    /* Average 4 samples to reduce noise */
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, BATT_ADC_CHANNEL, &raw) != ESP_OK) {
            return 0;
        }
        int mv = raw;
        if (s_cali_valid) {
            adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        }
        sum += mv;
    }
    int adc_mv = sum / 4;

    /* Compensate for on-board voltage divider */
    return adc_mv * BATT_DIVIDER;
}

void battery_monitor_format(char *buf, size_t len)
{
    if (!buf || len == 0) return;

    int mv = battery_monitor_read_mv();
    if (mv <= 0) {
        snprintf(buf, len, "?.??V ?%%");
        return;
    }

    /* Clamp and compute percentage */
    if (mv > BATT_FULL_MV)  mv = BATT_FULL_MV;
    if (mv < BATT_EMPTY_MV) mv = BATT_EMPTY_MV;
    int pct = (mv - BATT_EMPTY_MV) * 100 / (BATT_FULL_MV - BATT_EMPTY_MV);

    snprintf(buf, len, "%d.%02dV %d%%",
             mv / 1000, (mv % 1000) / 10, pct);
}
