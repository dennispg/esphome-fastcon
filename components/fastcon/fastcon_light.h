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

        // One of the bulbs' built-in effects (see FastconController::send_group_scene)
        struct FastconScene
        {
            bool flash{false};
            uint8_t speed{50};
            std::vector<uint8_t> steps;
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
            light::LightState *get_state() const { return state_; }
            // Which LEDs the bulb has, so Home Assistant only offers controls that do something
            void set_supports_rgb(bool supports) { supports_rgb_ = supports; }
            void set_supports_white(bool supports) { supports_white_ = supports; }

            // ---- Group lights ----
            // Send one group broadcast to group_id instead of a single-bulb command
            void set_group_id(uint8_t group_id)
            {
                group_id_ = group_id;
                is_group_ = true;
            }
            // A single-bulb fastcon light whose entity follows this group's state
            void add_member(light::LightState *member);
            // Write this group's number into each declared member bulb (fastcon.assign_members)
            void assign_members();
            bool is_group() const { return is_group_; }

            // Built-in effects (group lights only), called by FastconSceneEffect. start_scene sends the
            // effect, and until stop_scene, steady-colour writes are not sent (they would end it on the
            // bulbs). Turning off still goes out. ESPHome writes the state right after stopping an
            // effect, and that write sends the steady colour again.
            void start_scene(const FastconScene *scene);
            void stop_scene(const FastconScene *scene);

            // ---- Single-bulb lights ----
            // Show a group command's result on this bulb's entity. light_data is what the group
            // command already delivered, so the write this causes is not sent again.
            void mirror(const light::LightColorValues &values, const std::vector<uint8_t> &light_data);

        protected:
            // Declared members, or every single-bulb light for group 0 with none declared
            const std::vector<FastconLight *> &get_members_() const;
            std::vector<uint8_t> get_member_ids_() const;
            void send_scene_();

            FastconController *controller_{nullptr};
            light::LightState *state_{nullptr};
            uint8_t light_id_;
            bool supports_rgb_{true};
            bool supports_white_{true};

            bool is_group_{false};
            bool boot_write_pending_{true};
            uint8_t group_id_{0};
            std::vector<FastconLight *> members_;
            // The effect selected on this group light, and whether the bulbs are running it
            // (false after the group was turned off while the effect stayed selected)
            const FastconScene *scene_{nullptr};
            bool scene_sent_{false};

            std::vector<uint8_t> mirrored_data_;
        };
    } // namespace fastcon
} // namespace esphome
