#pragma once
// Music sync beat engine (examples/music-sync). Turns loudness readings from a microphone device
// (RMS and peak in dB, where 0 is the loudest the mic can measure), about 20 a second, into a
// brightness and color for a fastcon group: it adapts to the volume, spots beats (a sudden jump
// above the running average), pulses the brightness on them, and steps through a color palette.
// Include it from the controller's esphome: block (see controller.yaml).
#include <algorithm>
#include <cmath>
#include <cstdint>

struct MusicFrame {
  bool send{false};  // false: nothing changed enough to be worth a Bluetooth command
  uint8_t bri{0};    // 1-127
  uint8_t r{0}, g{0}, b{0};
};

class MusicSync {
 public:
  // palette: 0 Rainbow, 1 Party, 2 Fire, 3 Ocean, 4 Toxic, 5 Alarm
  MusicFrame update(float rms_db, float peak_db, uint32_t now, float sensitivity, float max_bri_pct, int palette) {
    MusicFrame out;
    if (std::isnan(rms_db))
      return out;
    const float level_db = rms_db;
    if (!this->init_) {
      this->floor_ = level_db;
      this->ceil_ = level_db + 6.0f;
      this->fast_ = this->slow_ = level_db;
      this->init_ = true;
    }
    // Adaptive range: the floor follows quiet passages (drops at once, creeps up slowly);
    // the ceiling follows loud ones (rises at once, decays slowly).
    this->floor_ = level_db < this->floor_ ? level_db : this->floor_ + 0.02f;
    this->ceil_ = level_db > this->ceil_ ? level_db : this->ceil_ - 0.05f;
    if (this->ceil_ < this->floor_ + 12.0f)
      this->ceil_ = this->floor_ + 12.0f;
    this->fast_ += 0.7f * (level_db - this->fast_);
    this->slow_ += 0.05f * (level_db - this->slow_);

    const float s = std::clamp(sensitivity, 0.0f, 100.0f) / 100.0f;
    const float gate_db = 20.0f - 14.0f * s;  // how far above the floor before the lights react
    const float beat_db = 6.5f - 4.0f * s;    // how big a jump counts as a beat
    float level = (level_db - this->floor_ - gate_db) / std::max(this->ceil_ - this->floor_ - gate_db, 3.0f);
    level = std::clamp(level, 0.0f, 1.0f);

    if (this->fast_ - this->slow_ > beat_db && now - this->last_beat_ > 180) {
      this->last_beat_ = now;
      this->flash_ = 1.0f;
      this->step_++;
    }

    const float base = 0.12f;  // a dim glow between beats
    const float max_bri = std::clamp(max_bri_pct, 5.0f, 100.0f) / 100.0f;
    const float v = base + (max_bri - base) * std::max(std::pow(level, 1.3f), this->flash_);
    this->flash_ *= 0.6f;  // the beat flash fades over the next few readings
    out.bri = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v * 127.0f)), 1, 127));

    this->hue_ = std::fmod(this->hue_ + 0.4f + 6.0f * level, 360.0f);
    this->pick_color_(palette, out);

    // Only send when the look changes noticeably, or once a second to keep the bulbs in step
    const bool color_changed = out.r != this->last_r_ || out.g != this->last_g_ || out.b != this->last_b_;
    if (std::abs(static_cast<int>(out.bri) - static_cast<int>(this->last_bri_)) >= 3 || color_changed ||
        now - this->last_send_ > 1000) {
      out.send = true;
      this->last_bri_ = out.bri;
      this->last_r_ = out.r;
      this->last_g_ = out.g;
      this->last_b_ = out.b;
      this->last_send_ = now;
    }
    return out;
  }

 protected:
  void pick_color_(int palette, MusicFrame &out) {
    static const uint8_t PARTY[][3] = {{255, 0, 0}, {255, 0, 160}, {0, 0, 255}, {0, 200, 255}, {0, 255, 0}, {255, 200, 0}};
    static const uint8_t FIRE[][3] = {{255, 0, 0}, {255, 70, 0}, {255, 140, 0}, {160, 0, 0}};
    static const uint8_t OCEAN[][3] = {{0, 40, 255}, {0, 200, 180}, {0, 110, 255}, {90, 0, 255}};
    static const uint8_t TOXIC[][3] = {{60, 255, 0}, {200, 255, 0}, {0, 160, 40}};
    static const uint8_t ALARM[][3] = {{255, 0, 0}, {120, 0, 0}};
    const uint8_t *c = nullptr;
    switch (palette) {
      case 1: c = PARTY[this->step_ % 6]; break;
      case 2: c = FIRE[this->step_ % 4]; break;
      case 3: c = OCEAN[this->step_ % 4]; break;
      case 4: c = TOXIC[this->step_ % 3]; break;
      case 5: c = ALARM[this->step_ % 2]; break;
      default: {  // Rainbow: drifts with the music and jumps on each beat
        hsv_to_rgb_(std::fmod(this->hue_ + 60.0f * (this->step_ % 6), 360.0f), out);
        return;
      }
    }
    out.r = c[0];
    out.g = c[1];
    out.b = c[2];
  }

  static void hsv_to_rgb_(float h, MusicFrame &out) {
    const float x = 1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = 1; g = x; }
    else if (h < 120) { r = x; g = 1; }
    else if (h < 180) { g = 1; b = x; }
    else if (h < 240) { g = x; b = 1; }
    else if (h < 300) { r = x; b = 1; }
    else { r = 1; b = x; }
    out.r = static_cast<uint8_t>(r * 255);
    out.g = static_cast<uint8_t>(g * 255);
    out.b = static_cast<uint8_t>(b * 255);
  }

  bool init_{false};
  float floor_{0}, ceil_{0}, fast_{0}, slow_{0}, flash_{0}, hue_{0};
  uint32_t last_beat_{0}, last_send_{0};
  uint32_t step_{0};
  uint8_t last_bri_{0}, last_r_{0}, last_g_{0}, last_b_{0};
};

// The one engine, for the lambdas in controller.yaml. A function rather than an ESPHome global,
// because ESPHome declares globals before this file is included, so they can't use its types.
inline MusicSync &music_engine() {
  static MusicSync engine;
  return engine;
}
