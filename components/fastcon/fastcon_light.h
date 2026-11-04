#pragma once

#include <array>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "fastcon_controller.h"

namespace esphome
{
    namespace fastcon
    {
        enum class LightState
        {
            OFF,
            WARM_WHITE,
            RGB
        };

        class FastconLight : public Component, public light::LightOutput
        {
        public:
            FastconLight(uint8_t light_id) : light_id_(light_id) {}

            void setup() override;
            void loop() override;
            light::LightTraits get_traits() override;
            void write_state(light::LightState *state) override;
            void set_controller(FastconController *controller);
            // The "all lights" entity sends one group command instead of a single-bulb one
            void set_group(bool group) { is_group_ = group; }
            void setup_state(light::LightState *state) override { state_ = state; }
            // Show a group command's result on this bulb's entity without sending anything
            void mirror(const light::LightColorValues &values);

        protected:
            FastconController *controller_{nullptr};
            uint8_t light_id_;
            bool is_group_{false};
            light::LightState *state_{nullptr};
            uint32_t mirror_until_{0};
            bool effect_was_active_{false};
        };
    } // namespace fastcon
} // namespace esphome
