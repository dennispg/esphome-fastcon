#pragma once
namespace esphome::esp32_ble {
struct ESP32BLE { bool active = true; bool is_active() { return active; } };
inline ESP32BLE test_ble;
inline ESP32BLE *global_ble = &test_ble;
}
