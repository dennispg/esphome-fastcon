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
            light::LightTraits get_traits() override;
            void write_state(light::LightState *state) override;
            void setup_state(light::LightState *state) override { state_ = state; }
            void set_controller(FastconController *controller);

            uint8_t get_light_id() const { return light_id_; }

            // ---- Group lights ----
            // Send one group broadcast to group_id instead of a single-bulb command
            void set_group_id(uint8_t group_id)
            {
                group_id_ = group_id;
                is_group_ = true;
            }
            // A single-bulb fastcon light whose entity follows this group's state
            void add_member(light::LightState *member);

            // ---- Single-bulb lights ----
            // Show a group command's result on this bulb's entity. light_data is what the group
            // command already delivered, so the write this causes is not sent again.
            void mirror(const light::LightColorValues &values, const std::vector<uint8_t> &light_data);

        protected:
            // Declared members, or every single-bulb light for group 0 with none declared
            const std::vector<FastconLight *> &get_members_() const;

            FastconController *controller_{nullptr};
            light::LightState *state_{nullptr};
            uint8_t light_id_;

            bool is_group_{false};
            bool boot_write_pending_{true};
            uint8_t group_id_{0};
            std::vector<FastconLight *> members_;

            std::vector<uint8_t> mirrored_data_;
        };
    } // namespace fastcon
} // namespace esphome
