#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
namespace esphome {
inline std::map<uint32_t, std::vector<uint8_t>> test_preferences;
inline bool test_storage_ok = true;
class ESPPreferenceObject {
public:
    uint32_t key = 0;
    template<typename T> bool load(T *out) {
        const auto found = test_preferences.find(key);
        if (found == test_preferences.end() || found->second.size() != sizeof(T)) return false;
        std::memcpy(out, found->second.data(), sizeof(T));
        return true;
    }
    template<typename T> bool save(const T *value) {
        if (!test_storage_ok) return false;
        const auto *bytes = reinterpret_cast<const uint8_t *>(value);
        test_preferences[key] = {bytes, bytes + sizeof(T)};
        return true;
    }
};
class Preferences {
public:
    template<typename T> ESPPreferenceObject make_preference(uint32_t key, bool) { return {key}; }
    bool sync() { return test_storage_ok; }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
}
