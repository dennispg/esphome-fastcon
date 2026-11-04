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
- Group control (all bulbs in a group change at the same instant)
- The bulbs' built-in effects (flash and fade sequences) on group lights

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

Each light is either a single bulb (`light_id`) or a group (`group_id`). Use exactly one of the two.

- **light_id** (*Optional*, int): The ID of a single bulb (1-255)
- **group_id** (*Optional*, int): A group number (0-255), controlled with a single broadcast. `0` is the brMesh app's "All" group. See [Group Control](#group-control)
- **members** (*Optional*, list of light IDs): Group lights only. The single-bulb lights in this group, so their entities follow the group's state. Group `0` without `members` follows every single-bulb light
- **effects** (*Optional*, list): ESPHome light effects. Group lights can also use the bulbs' own effects with `fastcon_scene`. See [Built-in effects](#built-in-effects)
- **name** (*Required*, string): The name for the light entity
- **id** (*Optional*, ID): The ID to use for this light component
- **controller_id** (*Optional*, ID): The ID of the controller to use. Defaults to "fastcon_controller"

## Group Control

The bulbs store a group number, and a group command is a single broadcast that every bulb in the group obeys, so they all change at the same instant instead of one after another. Group `0` is the brMesh app's "All" group and reaches every bulb with no setup.

Add a light with `group_id` to control a whole group as one entity:

```yaml
light:
  - platform: fastcon
    id: living_room_1
    name: "Living Room 1"
    light_id: 1
    default_transition_length: 0s
  - platform: fastcon
    id: living_room_2
    name: "Living Room 2"
    light_id: 2
    default_transition_length: 0s

  - platform: fastcon
    name: "Living Room"
    group_id: 1
    members: [living_room_1, living_room_2]
    default_transition_length: 0s
  - platform: fastcon
    name: "All Lights"
    group_id: 0                     # every bulb, no setup needed
    default_transition_length: 0s
```

When a group light changes, its new state is mirrored onto its members' entities without sending anything more to those bulbs, so Home Assistant stays in sync. A group without `members` still controls its bulbs, but their entities won't follow along (except group `0`, which mirrors to every single-bulb light).

At boot, a group light only restores its own entity's state. It doesn't broadcast it, so each bulb keeps its own restored state.

### Putting bulbs in a group

The bulbs decide which group broadcasts they obey: each bulb stores a group number. `members` only tells ESPHome which entities to keep in sync. The bulbs never report their group back, so the two have to match.

Groups you set up in the brMesh app work as they are, as long as you use the same group number. Otherwise, press a button with `fastcon.assign_members` once. It writes the group's number into every bulb in its `members`:

```yaml
light:
  - platform: fastcon
    id: living_room
    name: "Living Room"
    group_id: 1
    members: [living_room_1, living_room_2]

button:
  - platform: template
    name: "Assign Living Room bulbs to their group"
    on_press:
      - fastcon.assign_members: living_room
```

The bulbs remember their group, so you only need to press it again after changing `members`. It isn't known yet whether a bulb can belong to more than one group, so assigning a bulb to a new group may take it out of its old one. If a bulb doesn't respond, press it again, or set `repeat_count` on the controller.

To write a group into a single bulb, use `fastcon.set_group`. Group `0` removes the bulb from its group:

```yaml
    on_press:
      - fastcon.set_group:
          light_id: 2
          group_id: 1
```

Group numbers 253-255 are reserved by the app (temporary groups), and 193-252 are used for its sensor groups, so pick groups from 1-192.

### Built-in effects

The bulbs have their own flash and fade effects. Starting one takes a single group command, after which the bulbs run it by themselves with no further Bluetooth traffic. Add them to a group light as `fastcon_scene` effects, and pick them from the light's effect list like any other ESPHome effect:

```yaml
light:
  - platform: fastcon
    name: "Living Room"
    group_id: 1
    effects:
      - fastcon_scene:
          name: "Red Flash"
          mode: flash
          speed: 60
          steps: [red, "off"]
      - fastcon_scene:
          name: "Rainbow Fade"
          mode: fade
          steps: [red, yellow, green, cyan, blue, magenta]
```

- **name** (*Optional*, string): The effect's name. Defaults to `Scene`
- **mode** (*Optional*, `flash` or `fade`): `flash` jumps between the steps, `fade` fades between them. Defaults to `fade`
- **speed** (*Optional*, int, 1-100): Higher is faster. Defaults to 50
- **steps** (*Required*, list, 1-9 entries): The colours to go through. Each is one of `off`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta` (or `purple`), `white`, `warm_white`, `cold_white`, or a number from `0x00` to `0x1F` made by adding `0x01` blue, `0x02` red, `0x04` green, `0x08` warm white and `0x10` cold white. Quote `"off"`, or YAML may read it as `false` (which also works)

`fastcon_scene` only works on group lights (with `group_id`), since the bulbs only accept this command for a group. Starting an effect sends an "on" to the group first, so it also works from off.

While an effect runs, the group light doesn't send colour or brightness changes, because any steady-colour command ends the effect on the bulbs. Those changes are kept and go out when the effect is stopped: setting the effect back to None sends the group's current colour, and turning the light off always goes out. The members' entities aren't changed when an effect starts.

### Protocol notes

These layouts were worked out by analysing the brMesh Android app (5.92), for interoperability with the author's own lights. All use command type 5 and the same packet format as single-light control; only the inner payload differs:

| Command | Inner payload |
|---|---|
| Group control | `((len+3)<<4)\|3`, `2A A8` (light device type), group, then 1- or 6-byte light data |
| Built-in effect | `((k+2)<<4)\|0x0C`, group, speed (in 1/64 s ticks), then k steps (up to 9; first step `\| 0x40`, plus `\| 0x80` for flash) |
| Set a bulb's group | `21`, light id, group (forward bit clear) |

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

The group control and set-group frame formats were decoded by [leon101noel](https://github.com/leon101noel) and independently confirmed by [oriolfda](https://github.com/oriolfda/esphome-fastcon).

### Implementation References

- [ArcadeMachinist's brMeshMQTT](https://github.com/ArcadeMachinist/brMeshMQTT) - This work was crucial in helping me understand the practical implementation details of the protocol. https://github.com/ArcadeMachinist/brMeshMQTT

### Community Resources

- [Home Assistant Community Thread](https://community.home-assistant.io/t/brmesh-app-bluetooth-lights/473486/102)

This ESPHome component adapts and/or takes heavy inspiration from all of these works to run directly on ESP32 devices, allowing for native integration with Home Assistant without requiring additional bridges or MQTT brokers. A huge thank you to all those who contributed to my understanding of the Fastcon BLE protocol.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
