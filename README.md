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

#### Fastcon Light

- **light_id** (*Required*, int): The ID of the light (1-255)
- **name** (*Required*, string): The name for the light entity
- **id** (*Optional*, ID): The ID to use for this light component
- **controller_id** (*Optional*, ID): The ID of the controller to use. Defaults to "fastcon_controller"

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
