# Music sync

Make a fastcon group pulse and change color with the music in the room, using any ESPHome device with a microphone.

The mic device measures the room's loudness 20 times a second and sends each reading straight to the fastcon controller as one small UDP packet. Home Assistant isn't in the path, and only loudness leaves the device, never audio. On the controller, a beat engine adapts to the volume, spots beats (a sudden jump above the running average), pulses the brightness on them and steps through a color palette. The whole group then gets one command, only when the look changes noticeably: about 14 a second for loud music, 1 a second in silence. Because the controller keeps only the newest command per light or group, the lights never fall behind the music.

It reacts to any sound in the room, music or loud talking, so the sensitivity setting matters.

## Files

| File | Goes on | What it does |
|---|---|---|
| [`mic-device.yaml`](mic-device.yaml) | the device with the microphone | Measures loudness, sends it to the controller, adds a **Music Mic** switch |
| [`controller.yaml`](controller.yaml) | the fastcon controller | Receives the readings and drives a group light. Adds **Music Sync**, **Music Palette**, **Music Sensitivity**, **Music Max Brightness** and a **Music Level** readout |
| [`music_sync.h`](music_sync.h) | the fastcon controller | The beat engine |

## Setup

1. Give the fastcon controller a fixed IP address (a DHCP reservation in your router). The mic device sends to that address.
2. Copy this folder into your ESPHome config directory, as `music-sync/`.
3. On the mic device, add the package with the id of its microphone and the controller's address:

   ```yaml
   packages:
     music_mic: !include
       file: music-sync/mic-device.yaml
       vars:
         microphone: my_mic
         controller_ip: 192.168.1.50
   ```

4. On the fastcon controller, include the beat engine and add the package with the group light to drive. `group_id` must match that light's `group_id`:

   ```yaml
   esphome:
     includes:
       - music-sync/music_sync.h

   packages:
     music_sync: !include
       file: music-sync/controller.yaml
       vars:
         group_light: living_room
         group_id: 1
   ```

5. Install both. Turn on **Music Mic** on the mic device and **Music Sync** on the controller, and play something. **Music Level** on the controller should show a number in dB, about once a second.

Both packages use ESPHome's `udp` component on its default port (18511), and need Wi-Fi (or Ethernet) on both devices.

### One switch in Home Assistant

To turn the mic on and off with Music Sync, so only one switch is needed, add an automation (replace the entity IDs with yours):

```yaml
automation:
  - alias: "Music mic follows Music Sync"
    triggers:
      - trigger: state
        entity_id: switch.fastcon_controller_music_sync
        to: ["on", "off"]
      - trigger: state
        entity_id: switch.mic_device_music_mic
        from: [unavailable, unknown]
    conditions:
      - condition: template
        value_template: >-
          {{ states('switch.fastcon_controller_music_sync') in ['on', 'off']
             and states('switch.mic_device_music_mic') in ['on', 'off'] }}
    actions:
      - action: "switch.turn_{{ states('switch.fastcon_controller_music_sync') }}"
        target:
          entity_id: switch.mic_device_music_mic
```

## Controls

- **Music Sync** (controller): on, the group follows the music. Off, it goes back to what its light entity shows. If the readings stop for 3 seconds, the group also goes back, once.
- **Music Palette** (controller): Rainbow (drifts with the music and jumps on each beat), Party, Fire, Ocean, Toxic, Alarm. Each beat steps to the next color.
- **Music Sensitivity** (controller, 0-100): higher reacts to quieter sound and smaller beats. Start at 60. Lower it if talking sets the lights off.
- **Music Max Brightness** (controller): the brightness of a beat. Between beats the group glows at about 12%.
- **Music Mic** (mic device): starts and stops the measuring.

The group light's own effects take priority. While one runs (for example a `fastcon_scene` alarm), music sync pauses, and it carries on once the effect is set back to None.

## Troubleshooting

**Music Level stays blank.**

- Check that Music Mic is on, and that the mic device is online.
- Check that `controller_ip` is still the controller's address.
- Make sure the mic is actually delivering audio. On some boards, the speaker codec drives the I2S clock that the microphone also runs on (for example an ES8311 forced to I2S master next to an ES7210), so the mic hears nothing until the speaker has played something since boot. Playing a short silent clip from `on_boot` fixes that.

**The lights lag or stutter.** Set `power_save_mode: none` under `wifi:` on the mic device, so its packets go out at once. On the controller, the [tuning settings](../../README.md#tuning-for-speed-and-reliability) for speed apply.

**The lights react to talking.** Lower Music Sensitivity.
