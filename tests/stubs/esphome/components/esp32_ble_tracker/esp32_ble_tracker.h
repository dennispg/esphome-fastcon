#pragma once
#include <cstdint>
#include <vector>
#include <esp_gap_ble_api.h>
#include "esphome/core/component.h"
#include "esphome/components/esp32_ble/ble.h"
namespace esphome::esp32_ble_tracker {
struct ESPBTUUID {
    uint16_t uuid;
    static ESPBTUUID from_uint16(uint16_t id) { return {id}; }
    bool operator!=(const ESPBTUUID &other) const { return uuid != other.uuid; }
};
struct ManufacturerData { ESPBTUUID uuid{0xfff0}; std::vector<uint8_t> data; };
struct ESPBTDevice {
    uint64_t mac = 1;
    std::vector<ManufacturerData> manufacturers;
    const auto &get_manufacturer_datas() const { return manufacturers; }
    uint64_t address_uint64() const { return mac; }
};
class ESPBTDeviceListener {
public:
    virtual ~ESPBTDeviceListener() = default;
    virtual bool parse_device(const ESPBTDevice &) = 0;
    virtual void on_scan_end() {}
};
class ESP32BLETracker {
public:
    bool scanning = false;
    int starts = 0, stops = 0;
    void set_scan_continuous(bool) {}
    void set_scan_duration(uint32_t) {}
    void set_scan_active(bool) {}
    bool start_pending = false, stop_pending = false;
    bool fail_start = false, fail_stop = false;
    uint32_t start_delay_ms = 0, requested_at = 0;
    void start_scan() { start_pending = true; requested_at = millis(); starts++; }
    void stop_scan() { scanning = false; stop_pending = true; stops++; }
    template<typename Controller> void tick(Controller &controller) {
        esp_ble_gap_cb_param_t param{};
        if (start_pending && millis() - requested_at >= start_delay_ms) {
            start_pending = false;
            scanning = !fail_start;
            param.scan_start_cmpl.status = fail_start ? 1 : ESP_BT_STATUS_SUCCESS;
            controller.gap_event_handler(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT, &param);
            // ESPHome cleanup notifies scan-end even after async start failure.
            if (fail_start) controller.on_scan_end();
        }
        if (stop_pending) {
            stop_pending = false;
            param.scan_stop_cmpl.status = fail_stop ? 1 : ESP_BT_STATUS_SUCCESS;
            controller.gap_event_handler(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT, &param);
            controller.on_scan_end();
        }
    }
};
}
