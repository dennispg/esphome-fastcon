# ESPHome Fastcon BLE Light Component

[![Compile](https://github.com/dennispg/esphome-fastcon/actions/workflows/compile.yml/badge.svg)](https://github.com/dennispg/esphome-fastcon/actions/workflows/compile.yml)

This is a custom component for ESPHome that allows you to control Broadlink Fastcon BLE lights, also known as brMesh. It should work with any light that can be controlled by brMesh or Broadlink BLE mobile apps.

Be warned - there is also a brLight app, which might look like brMesh, but the protocol is different.

## Requirements

- ESP32 board (`arduino` or `esp-idf` framework)
- ESPHome 2023.12.0 or newer (CI compiles against the latest stable and beta releases every week)

## Supported Features

- On/Off control
- Brightness control
- RGB color control
- White mode
- Group control (all bulbs change at the same instant)
- The bulbs' built-in effects (flash and fade sequences)

## Configuration

Add the following to your ESPHome configuration:

```yaml
# ESP32 is required
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: arduino

esp32_ble_tracker:
esp32_ble_server:

# Source configuration (pinned to a release; use @main for the latest unreleased changes)
external_components:
  - source: github://dennispg/esphome-fastcon@v1.1.0

# Controller configuration
fastcon:
  mesh_key: "12345678"    # Your mesh key in hex format

  # Optional parameters to control the advertisdement protocol with their defaults:
  adv_interval_min: 0x20  # Minimum advertisement interval
  adv_interval_max: 0x40  # Maximum advertisement interval
  adv_duration: 50        # Advertisement duration in milliseconds
  adv_gap: 10             # Gap between advertisements in milliseconds
  max_queue_size: 100     # Maximum number of queued commands
  repeat_count: 1         # How many times each command is broadcast

# Light configuration (add an entry for each light)
light:
  - platform: fastcon
    id: living_room_light
    name: "Living Room Light"
    light_id: 1           # ID of the light (1-255)
```

### Configuration Variables

#### Fastcon Controller

- **mesh_key** (*Required*, string): The mesh key for your Fastcon lights in hexadecimal format (8 characters/4 bytes)
- **id** (*Optional*, ID): The ID to use for this controller component. Defaults to "fastcon_controller"
- **adv_interval_min** (*Optional*, int): Minimum advertisement interval. Defaults to 0x20
- **adv_interval_max** (*Optional*, int): Maximum advertisement interval. Defaults to 0x40
- **adv_duration** (*Optional*, int): Duration of each advertisement in milliseconds. Defaults to 50
- **adv_gap** (*Optional*, int): Gap between advertisements in milliseconds. Defaults to 10
- **max_queue_size** (*Optional*, int): Maximum number of commands that can be queued. Defaults to 100
- **repeat_count** (*Optional*, int, 1-5): How many times each command is broadcast. Repeats go round-robin across the lights, so a bulb that misses one copy catches the next one shortly after. Defaults to 1

The queue keeps at most one pending command per light: a newer command for a light replaces one that is still waiting (including a pending repeat), so stale states never pile up behind each other and lights always end in the latest state.

### Tuning for speed and reliability

With 8 bulbs, this combination made every command land on the first tap while roughly halving the time for all bulbs to change (about 0.26 s for the first copy to reach all 8, versus about 0.42 s with the defaults):

```yaml
esp32_ble_tracker:
  scan_parameters:
    continuous: false     # nothing needs scanning; leaves the radio free for advertising

fastcon:
  mesh_key: "12345678"
  adv_interval_min: 0x20  # 20 ms, the shortest advertising interval Bluetooth allows
  adv_interval_max: 0x20
  adv_duration: 35        # about 2 advertising events per send
  adv_gap: 2
  repeat_count: 2         # every command twice, round-robin across the lights

light:
  - platform: fastcon
    name: "Light 1"
    light_id: 1
    default_transition_length: 0s   # transitions would queue a burst of intermediate commands
```

Note: the ESP32's Bluetooth stack rejects advertising intervals below 0x20, so values like 0x06 fail to advertise.

#### Fastcon Light

- **light_id** (*Required*, int): The ID of the light (1-255)
- **name** (*Required*, string): The name for the light entity
- **id** (*Optional*, ID): The ID to use for this light component
- **controller_id** (*Optional*, ID): The ID of the controller to use. Defaults to "fastcon_controller"

## Group Control and Built-in Effects

The bulbs store a group number, and a group command is a single broadcast that every bulb in the group obeys, so they all change at the same instant instead of one after another. Group `0` is the brMesh app's "All" group and reaches every bulb with no setup.

Add a light with `group: true` to control the whole group as one entity. Its state is mirrored onto the individual lights' entities (without sending anything to them), so Home Assistant stays in sync:

```yaml
light:
  - platform: fastcon
    id: all_lights
    name: "All Lights"
    group: true                     # one group command instead of a single bulb
    default_transition_length: 0s
    effects:                        # the bulbs' own built-in effects, see below
      - lambda:
          name: "Red flash"
          update_interval: 1h
          lambda: |-
            if (initial_run) id(fastcon_controller).send_group_scene(true, id(effect_speed).state, {0x02, 0x00});
      - lambda:
          name: "Full colour fade"
          update_interval: 1h
          lambda: |-
            if (initial_run) id(fastcon_controller).send_group_scene(false, id(effect_speed).state, {0x02, 0x03, 0x01, 0x05, 0x04, 0x06});
  - platform: fastcon
    name: "Light 1"
    light_id: 1
    default_transition_length: 0s

number:
  - platform: template
    id: effect_speed
    name: "Effect Speed"            # 1-100, higher is faster; read when an effect starts
    min_value: 1
    max_value: 100
    step: 1
    initial_value: 60
    optimistic: true
    restore_value: true
```

The group light addresses group 0 by default. To use another group, call `id(fastcon_controller).set_group_id(n)` (for example from a template `number`), and write the group into each bulb with `id(fastcon_controller).send_set_group(light_id, n)`. The bulb stores it; `0` removes it from its group.

### Built-in effects

`send_group_scene(flash, speed, steps)` starts one of the bulbs' own effects: one command, after which the bulbs run the effect by themselves with no further Bluetooth traffic. It keeps running until the next command (choosing another colour, turning off, or setting the effect to None).

- **flash**: `true` jumps between the steps, `false` fades between them.
- **speed**: 1-100, higher is faster.
- **steps**: up to 9 colours, each a combination of `0x01` blue, `0x02` red, `0x04` green, `0x08` warm white, `0x10` cold white, or `0x00` off. For example `{0x02, 0x00}` is a red flash, `{0x02, 0x01}` alternates red and blue, and `{0x06, 0x03}` fades between yellow and purple.

Starting an effect also sends an "on" first, so it works from off. While an effect runs, the group light doesn't send steady-colour updates (they would cancel it), and stopping the effect sends the steady colour again.

### Protocol notes

These layouts were worked out by analysing the brMesh Android app (5.92), for interoperability with the author's own lights. All use command type 5 and the same packet format as single-light control; only the inner payload differs:

| Command | Inner payload |
|---|---|
| Group control | `((len+3)<<4)\|3`, `2A A8` (light device type), group, then 1- or 6-byte light data |
| Built-in effect | `((k+2)<<4)\|0x0C`, group, speed (in 1/64 s ticks), then k steps (first step `\| 0x40`, plus `\| 0x80` for flash) |
| Set a bulb's group | `21`, light id, group (forward bit clear) |

Group numbers 253-255 are reserved by the app (temporary groups), and 193-252 are used for its sensor groups.

## Finding Your Mesh Key

The mesh key is crucial for controlling your Fastcon BLE lights. To find your light's mesh key, you first need to setup your devices using an Android device. The app generates a unique mesh key that will be used with all lights that are set up in the app.

Once the lights are setup, you can use ADB to connect to your phone and you may use the following command to extract the mesh key.

```bash
adb logcat | { grep -m 1 -o 'jyq_helper: .* payload:.\{24\},[[:space:]]*key:[[:space:]]*.\{8\}' | awk '{print $NF}'; kill -2 $(pgrep -P $$ adb); }
```

While running the above, open the app and toggle a light on and off. The command should then output your mesh key.

## Acknowledgments

This component builds upon the reverse engineering and hard work of several others who must be acknowledged and thanked:

### Protocol Reverse Engineering

The foundational protocol reverse engineering work was done by [Mooody](https://mooody.me/posts/2023-04/reverse-the-fastcon-ble-protocol/), who provided detailed analysis of the Fastcon BLE protocol, including packet structure and encryption methods. https://mooody.me/posts/2023-04/reverse-the-fastcon-ble-protocol/

### Implementation References

- [ArcadeMachinist's brMeshMQTT](https://github.com/ArcadeMachinist/brMeshMQTT) - This work was crucial in helping me understand the practical implementation details of the protocol. https://github.com/ArcadeMachinist/brMeshMQTT

### Community Resources

- [Home Assistant Community Thread](https://community.home-assistant.io/t/brmesh-app-bluetooth-lights/473486/102)

This ESPHome component adapts and/or takes heavy inspiration from all of these works to run directly on ESP32 devices, allowing for native integration with Home Assistant without requiring additional bridges or MQTT brokers. A huge thank you to all those who contributed to my understanding of the Fastcon BLE protocol.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
