#pragma once
namespace esphome {
template<typename... Ts> inline void test_log(const Ts &...) {}
}
#define ESP_LOGV(...) esphome::test_log(__VA_ARGS__)
#define ESP_LOGD(...) esphome::test_log(__VA_ARGS__)
#define ESP_LOGI(...) esphome::test_log(__VA_ARGS__)
#define ESP_LOGW(...) esphome::test_log(__VA_ARGS__)
#define ESP_LOGE(...) esphome::test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) esphome::test_log(__VA_ARGS__)
