#pragma once
#include <cstdint>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_BT_STATUS_SUCCESS = 0;
constexpr int BLE_ADDR_TYPE_PUBLIC = 0, ADV_TYPE_NONCONN_IND = 3, ADV_CHNL_ALL = 7;
constexpr int ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY = 0, ESP_BLE_AD_TYPE_FLAG = 1;
constexpr int ESP_BLE_ADV_FLAG_BREDR_NOT_SPT = 4, ESP_BLE_ADV_FLAG_GEN_DISC = 2;
constexpr int ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE = 0xff;
struct esp_ble_adv_params_t {
    uint16_t adv_int_min, adv_int_max;
    int adv_type, own_addr_type;
    uint8_t peer_addr[6];
    int peer_addr_type, channel_map, adv_filter_policy;
};
enum esp_gap_ble_cb_event_t {
    ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, ESP_GAP_BLE_ADV_START_COMPLETE_EVT,
    ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT
};
struct esp_ble_gap_cb_param_t {
    struct Status { int status = 0; } adv_data_raw_cmpl, adv_start_cmpl, adv_stop_cmpl;
};
inline std::vector<std::vector<uint8_t>> test_advertisements;
inline int test_advertise_result = ESP_OK;
inline const char *esp_err_to_name(int) { return "test error"; }
inline int esp_ble_gap_config_adv_data_raw(uint8_t *data, uint8_t length) {
    test_advertisements.emplace_back(data, data + length);
    return test_advertise_result;
}
inline int esp_ble_gap_start_advertising(esp_ble_adv_params_t *) { return test_advertise_result; }
inline int esp_ble_gap_stop_advertising() { return ESP_OK; }
