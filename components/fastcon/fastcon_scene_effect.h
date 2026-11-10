#pragma once

#include <vector>
#include "esphome/components/light/light_effect.h"
#include "fastcon_light.h"

namespace esphome
{
    namespace fastcon
    {
        // The fastcon_scene and fastcon_colors light effects: one of the bulbs' own flash/fade effects
        // (built-in steps, or custom colors) on a group light.
        // Starting it sends a single command, after which the bulbs run the effect by themselves,
        // so apply() has nothing to do.
        class FastconSceneEffect : public light::LightEffect
        {
        public:
            explicit FastconSceneEffect(const char *name) : light::LightEffect(name) {}

            void set_flash(bool flash) { scene_.flash = flash; }
            void set_speed(uint8_t speed) { scene_.speed = speed; }
            void set_steps(const std::vector<uint8_t> &steps) { scene_.steps = steps; }
            void add_color(uint8_t red, uint8_t green, uint8_t blue) { scene_.colors.push_back({red, green, blue}); }
            void set_brightness(int brightness) { scene_.brightness = brightness; }

            void start() override;
            void stop() override;
            void apply() override {}

        protected:
            // Config validation only allows this effect on fastcon lights
            FastconLight *get_light_() const { return static_cast<FastconLight *>(this->state_->get_output()); }

            FastconScene scene_;
        };
    } // namespace fastcon
} // namespace esphome
