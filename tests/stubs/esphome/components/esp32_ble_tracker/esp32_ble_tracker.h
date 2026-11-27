#pragma once
#include <cstdint>
#include <vector>
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
    void start_scan() { scanning = true; starts++; }
    void stop_scan() { scanning = false; stops++; }
};
}
