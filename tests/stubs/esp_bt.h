#pragma once
#include "esp_gap_ble_api.h"
enum esp_power_level_t { ESP_PWR_LVL_P9 = 7 };
constexpr int ESP_BLE_PWR_TYPE_ADV = 0;
inline int esp_ble_tx_power_set(int, esp_power_level_t) { return ESP_OK; }
inline int esp_ble_tx_power_get(int) { return 0; }
