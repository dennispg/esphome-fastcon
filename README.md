# ESPHome Fastcon BLE Light Component

[![Compile](https://github.com/dennispg/esphome-fastcon/actions/workflows/compile.yml/badge.svg)](https://github.com/dennispg/esphome-fastcon/actions/workflows/compile.yml)

This is a custom component for ESPHome that allows you to control Broadlink Fastcon BLE lights, also known as brMesh. It should work with any light that can be controlled by brMesh or Broadlink BLE mobile apps.

Be warned - there is also a brLight app, which might look like brMesh, but the protocol is different.

## Requirements

- ESP32 board (`arduino` or `esp-idf` framework)
- ESPHome 2026.3.0 or newer (CI compiles against the latest stable and beta releases every week)

## Supported Features

- On/Off control
- Brightness control
- RGB color control
- White mode
- Turning on and off keep the bulb's own color, like the brMesh app
- Per-light capabilities for RGB-only, white-only and single-white bulbs
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

# Required by the component. Don't add esp32_ble_tracker unless something else
# needs it: see "Bulbs don't respond" under Troubleshooting.
esp32_ble_server:

# Source configuration (pinned to a release; use @main for the latest unreleased changes)
external_components:
  - source: github://dennispg/esphome-fastcon@v1.1.1

# Controller configuration
fastcon:
  mesh_key: "12345678"    # Your mesh key in hex format

  # Optional parameters to control the advertisement protocol with their defaults:
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
    default_transition_length: 0s   # transitions would send a burst of intermediate commands

  # Optional: several bulbs switched together by one command (see Group Control)
  - platform: fastcon
    name: "Living Room"
    group_id: 1
    members: [living_room_light]
    default_transition_length: 0s

# Optional: press once to write each group's number into its member bulbs
button:
  - platform: template
    name: "Assign bulbs to their groups"
    on_press:
      - fastcon.assign_members
```

### Configuration Variables

#### Fastcon Controller

- **mesh_key** (*Required*, string): The mesh key in hexadecimal format (8 characters/4 bytes), or `auto` to opt into [Auto-Discovery](#auto-discovery)
- **id** (*Optional*, ID): The ID to use for this controller component. Defaults to "fastcon_controller"
- **adv_interval_min** (*Optional*, int): Minimum advertisement interval. Defaults to 0x20
- **adv_interval_max** (*Optional*, int): Maximum advertisement interval. Defaults to 0x40
- **adv_duration** (*Optional*, int): Duration of each advertisement in milliseconds. Defaults to 50
- **adv_gap** (*Optional*, int): Gap between advertisements in milliseconds. Defaults to 10
- **max_queue_size** (*Optional*, int): Maximum number of commands that can be queued. Defaults to 100
- **repeat_count** (*Optional*, int, 1-5): How many times each command is broadcast. Repeats go round-robin across the lights, so a bulb that misses one copy catches the next one shortly after. Defaults to 1
- **tx_power** (*Optional*, int, dBm): Advertising transmit power: one of -12, -9, -6, -3, 0, 3, 6 or 9. Try `9` if bulbs far from the controller miss commands. Defaults to the Bluetooth stack's own setting, which the controller logs at boot

If the Bluetooth controller rejects an advertisement, the log shows a `fastcon.controller` warning such as `failed to start advertising (status N)`. Without it, a command that never went out would look the same in the logs as one that did.

The queue keeps at most one pending command per light: a newer command for a light replaces one that is still waiting (including a pending repeat), so stale states never pile up behind each other and lights always end in the latest state.

### Tuning for speed and reliability

The defaults are deliberately conservative. With 8 bulbs, this combination made every command land on the first tap while roughly halving the time for all bulbs to change (about 0.26 s for the first copy to reach all 8, versus about 0.42 s with the defaults). It needs a controller that isn't scanning (see [Troubleshooting](#troubleshooting)):

```yaml
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

If bulbs miss commands with these settings, raise `adv_duration` (100-200 ms) or set `tx_power: 9`. Group lights send one command for the whole group, so even slow settings stay quick for them.

Note: the ESP32's Bluetooth stack rejects advertising intervals below 0x20, so values like 0x06 fail to advertise.

#### Fastcon Light

Each light is a single bulb (`light_id`), a group (`group_id`), or an auto-discovery slot (`slot`). Use exactly one.

- **light_id** (*Optional*, int): The ID of a single bulb (1-255)
- **slot** (*Optional*, int): A runtime-bound discovery slot (1..`discovery.max_lights`); requires `mesh_key: auto`
- **group_id** (*Optional*, int): A group number (0-255), controlled with a single broadcast. `0` is the brMesh app's "All" group. See [Group Control](#group-control)
- **members** (*Optional*, list of light IDs): Group lights only. The single-bulb lights in this group, so their entities follow the group's state. Group `0` without `members` follows every single-bulb light
- **supports_white** (*Optional*, boolean): Set to `false` for RGB-only bulbs, which have no white LEDs. Home Assistant then offers only the color wheel, and white comes from the RGB channels. Defaults to `true`
- **supports_rgb** (*Optional*, boolean): Set to `false` for white-only bulbs, which have only cold/warm white LEDs. Home Assistant then offers only color temperature. Defaults to `true`
- **supports_color_temperature** (*Optional*, boolean): Set to `false` for bulbs with a single white and no adjustable color temperature (the brMesh app offers just one "white"). Home Assistant then offers the color wheel and a white button, but no color temperature slider. Has no effect when `supports_white` is `false`. Defaults to `true`
- **effects** (*Optional*, list): ESPHome light effects. Group lights can also use the bulbs' own effects with `fastcon_scene`. See [Built-in effects](#built-in-effects)
- **name** (*Required*, string): The name for the light entity
- **id** (*Optional*, ID): The ID to use for this light component
- **controller_id** (*Optional*, ID): The ID of the controller to use. Defaults to "fastcon_controller"

Like the brMesh app, turning a light on or off at the brightness it already had doesn't send a color: the bulb keeps whatever color it last had. Changing the brightness (also turning on at a new one) sends the light's color along with it, because some bulbs ignore a brightness sent on its own. On a group light, that means dimming the group also gives every bulb in it the group's color. A color is also sent when you change it, or when you pick the color a light that is already on already has, which re-applies it (for example to a group whose bulbs were changed one by one). If you change a bulb's color in the brMesh app, Home Assistant keeps showing the old color until you set one from Home Assistant.

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

Groups you set up in the brMesh app work as they are, as long as you use the same group number. Otherwise, press a button with `fastcon.assign_members` once. It writes each group's number into every bulb in that group's `members`, for all your group lights at once:

```yaml
button:
  - platform: template
    name: "Assign bulbs to their groups"
    on_press:
      - fastcon.assign_members
```

To assign just one group, give its light's `id`:

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

The bulbs remember their group, so you only need to press it again after changing `members`. Group `0` and groups without `members` are skipped, since there's nothing to write. It isn't known yet whether a bulb can belong to more than one group, so assigning a bulb to a new group may take it out of its old one. The config check warns if a bulb is listed in more than one numbered group. If a bulb doesn't respond, press it again, or set `repeat_count` on the controller.

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
- **steps** (*Required*, list, 1-9 entries): The colors to go through. Each is one of `off`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta` (or `purple`), `white`, `warm_white`, `cold_white`, or a number from `0x00` to `0x1F` made by adding `0x01` blue, `0x02` red, `0x04` green, `0x08` warm white and `0x10` cold white. Quote `"off"`, or YAML may read it as `false` (which also works)

`fastcon_scene` only works on group lights (with `group_id`), since the bulbs only accept this command for a group. Starting an effect sends an "on" to the group first, so it also works from off.

While an effect runs, the group light doesn't send color or brightness changes, because any steady-color command ends the effect on the bulbs. Those changes are kept and go out when the effect is stopped: setting the effect back to None sends the group's current color, and turning the light off always goes out. The members' entities aren't changed when an effect starts.

### Protocol notes

These layouts were worked out by analysing the brMesh Android app (5.92), for interoperability with the author's own lights. All use command type 5 and the same packet format as single-light control; only the inner payload differs:

| Command | Inner payload |
|---|---|
| Group control | `((len+3)<<4)\|3`, `2A A8` (light device type), group, then 1- or 6-byte light data |
| Built-in effect | `((k+2)<<4)\|0x0C`, group, speed (in 1/64 s ticks), then k steps (up to 9; first step `\| 0x40`, plus `\| 0x80` for flash) |
| Set a bulb's group | `21`, light id, group (forward bit clear) |

## Auto-Discovery

Auto-discovery is **opt-in and experimental**. Existing hexadecimal `mesh_key` configurations do not scan, pair or change bulb keys. FastCon uses manufacturer-specific BLE **advertisements**, not GATT connections or Bluetooth SIG Mesh. Discovery responses/heartbeats can confirm membership; they are not command acknowledgments or light-state telemetry. Home Assistant state remains optimistic.

### Flashing four lights once

Flash [`examples/auto-discovery.yaml`](examples/auto-discovery.yaml) from a checkout of this repository: select the ESP32 board, replace the placeholder `wifi_ssid` and `wifi_password` in `examples/secrets.yaml`, and keep your usual API encryption and OTA credentials. It defines four compile-time entities, so no second build is needed after finding bulbs. The test configurations in `tests/` are compile-only and do not configure Wi-Fi or the Home Assistant API; **do not flash those as a ready-to-use bridge**.

For an existing ESPHome device, keep its normal network/API/OTA configuration and add:

```yaml
esp32_ble_server:
esp32_ble_tracker:
  scan_parameters:
    continuous: false
    active: false
    duration: 1s
    interval: 50ms
    window: 25ms

external_components:
  - source: github://dennispg/esphome-fastcon@main

fastcon:
  mesh_key: auto
  discovery:
    max_lights: 4       # 1..16, default 4
    scan_timeout_s: 30  # 1..600, default 30; factory-discovery phase only

light:
  - platform: fastcon
    name: "Fastcon 1"
    slot: 1
    default_transition_length: 0s
  - platform: fastcon
    name: "Fastcon 2"
    slot: 2
    default_transition_length: 0s
  - platform: fastcon
    name: "Fastcon 3"
    slot: 3
    default_transition_length: 0s
  - platform: fastcon
    name: "Fastcon 4"
    slot: 4
    default_transition_length: 0s
```

`discovery` is optional with `mesh_key: auto`; its defaults are shown above. It is rejected with an explicit key. Keep all advertising intervals at least `0x20`. The existing advertisement timing, queue, repeat and TX-power options still apply to normal control. Recovery intentionally sends a single command per candidate regardless of `repeat_count`.

A `slot` is mutually exclusive with `light_id` and `group_id`, must be within `discovery.max_lights`, and cannot be declared twice. Capability flags work as on explicit lights. Slot names/IDs are stable across reboots; rename them in Home Assistant after identifying the physical bulbs. Explicit single-light addresses are reserved during factory assignment. Group `0` still controls all bulbs sharing the controller key, including bulbs beyond the configured slots. Slots can be group members; unbound slots are skipped when assigning groups.

**Availability limitation:** ESPHome's native light API has no per-light unavailable/unknown-state field. Unbound slots therefore remain visible and may show an optimistic/restored state, but send **no** control advertisements and carry a component warning until bound. They cannot truthfully be displayed as `unavailable` by this external component alone. A bound slot becomes controllable when discovery completes; a failed search leaves slots inert. This is not a bulb-health or presence monitor.

### Fast manual reclaim of app-paired bulbs

The primary protocol reference is [AdysTech's byte-level capture](https://github.com/AdysTech/br-mesh-controller/blob/main/protocol.md), including its boot-window keying lifecycle (§2.9). On those bulbs, **every boot briefly opens a pairing window**: WAKE heard then allows SET_KEY to replace the saved key. There is no physical reset switch. Simply rebooting without WAKE keeps the old key.

For a fast takeover, start discovery, then **power-cycle each intended bulb one at a time while WAKE is running**. The reference firmware reclaims a bulb in seconds; timing on this ESPHome port still needs hardware validation. Each distinct reversed-MAC identity gets its own light number, while all four get the **same newly generated controller key**. Cycle all four within the factory scan window (raise `scan_timeout_s`, for example to 120, if needed). Their four slot entities then control them independently. Only power-cycle bulbs intended for this controller: this deliberately replaces their app key.

The no-power-cycle 10,000-key fallback does **not** lock out this fast path. It interleaves a short WAKE/listen round every four candidates; a factory identity immediately pauses recovery and starts a fresh bounded factory-keying window. After completion or exhaustion, scanning/WAKE stops. This is initial onboarding, not an always-on incremental-enrollment service.

The app **rotates its mesh key on add/room changes** (§2.7). A recovered app key is only valid until it is changed again. A successful no-power-cycle recovery keeps that existing key; power-cycle/WAKE reclaim instead installs a controller-owned key. Close the app during discovery and avoid re-adding/re-grouping the bulbs in it afterwards.

### What happens at first boot

1. **Restore first.** One versioned ESPHome Preferences/NVS record per controller holds the shared four-byte mesh key, stable bulb IDs, claimed flags, identity bytes/MAC addresses when available, phase and recovery checkpoint. A completed discovery restores immediately without scanning or re-keying any bulb.
2. **Phase A — factory bulbs.** Power-cycle the unclaimed bulbs during the configured scan window. WAKE advertisements (at least 400 ms) and per-bulb SET_KEY advertisements (at least 300 ms) alternate with short passive scans (about 200 ms) and a rest interval; these waits are state-machine timers, not blocking delays. The matcher accepts both the captured **18-byte manufacturer representation** (`F0 FF` included) and the **16-byte old-protocol body** supplied after ESPHome strips the company ID. The cleartext factory key `5E 36 7B C4` is at manufacturer bytes `[14..17]` (body `[12..15]`). Every distinct reversed-MAC identity is collected independently, ignoring repeated nonce changes. Each bulb receives the next unused 1-based light ID and the controller's randomly generated **shared** key using SET_KEY under the factory key. A heartbeat passing the byte-2 layout and checksum checks confirms the binding. Pending identities and their key are flushed before SET_KEY, so a reboot cannot forget a key already sent to a bulb. Confirmed bulbs are never re-keyed on restore. Stop early when `max_lights` are confirmed; otherwise stop after the timeout plus the in-flight radio round. The default factory phase takes up to roughly 30 seconds.
3. **Phase B — existing app-keyed bulbs.** Only when Phase A found no factory candidates, try the app's 10,000 ASCII-digit keys (`"0000"` through `"9999"`, represented as four bytes). Each candidate gets **one group-0 ON command at full brightness, keeping the current color**, followed by a short scan. An extra WAKE/listen round (at least 100 ms advertising) every four candidates keeps boot-window reclaim reachable without repeating candidate group commands. A matching heartbeat is checked by decrypting the body, checking the reference safe-key or captured flood-light number convention, checksum, heartbeat opcode and address range. The winning command visibly turns on **every nearby bulb on that mesh**, not just the slots. Then probe addresses **1..16**, binding confirmed addresses in ascending order up to `max_lights`; re-assert group ON/full brightness at completion. Addresses above 16, non-app random keys and additional meshes are not exhaustively searched. If the factory phase claimed any bulbs, other app-keyed meshes are not searched.
4. **Phase C — no confirmation.** Log a warning and stop; no continuous scan or automatic endless retry. Use the [ADB fallback](#finding-your-mesh-key-fallback), then apply the key at runtime or switch to an explicit-key configuration. If factory candidates remain unconfirmed (including partial success), retain the resumable FACTORY phase and pending records rather than discarding a possibly installed key. Radio work stops for this boot and confirmed bulbs remain controllable. Reboot to probe pending bulbs with those same saved identities/key; confirmed bulbs are not re-keyed. A radio/storage error also stops without silently advancing the candidate.

For pending identities, factory SET_KEY retries alternate with saved-key addressed probes within the factory timeout. On reboot, a restored pending identity whose nonzero saved advertiser MAC matches a checksum-valid saved-key heartbeat reporting a different number triggers up to three identity-addressed SET_KEY attempts under the saved mesh key, interleaved with probes. A shared advertiser MAC can trigger repair for multiple pending identities, each with its own DID and retry budget. This repair is experimental: the checksum is weak, advertiser MACs may change, and the saved-key SET_KEY path has not been hardware-verified. It does not recover a different app key.

Heartbeat byte 2 must match either the reference safe-key byte (`key[3]`) or the light number in byte 5, as in the captured flood-light layout. Both variants still require the heartbeat opcode, address and checksum checks. This is not authentication and does not guarantee that a candidate key is correct.

The slow path is I/O-bound and nonblocking: no busy key-search loop or blocking delays. With defaults, a full sweep is on the order of **an hour or more**, not seconds. The synthetic 10 ms-loop test takes about **73 minutes** including the interleaved reclaim WAKE rounds; budget **1–2 hours**, with no guaranteed wall-clock maximum because Wi-Fi coexistence, scanner startup/stop latency and longer advertising durations can make it substantially longer. This is a software timing model, not a radio benchmark. Progress is logged every 500 keys and checkpointed every 50 to limit flash wear; a reboot can repeat up to 49 candidates, but does not restart the whole search. Completed/exhausted searches do not repeat on every boot. Enumeration takes several more seconds.

Discovery scanning stops after completion/failure. Do not run other scan-start automations during discovery, or turn on continuous scanning: it competes with the advertising used for control. The short listen windows and weak protocol checksum are **not a guarantee of recovery**: missed heartbeats, periodic advertisements, variant firmware and unrelated mesh traffic need testing on real bulbs. Keep the phone app closed while discovering. Key confirmation is independent of slot allocation: a valid heartbeat from an explicitly reserved address (or an address outside the 1..16 slot-probe range) still confirms the mesh key. Explicit lights remain controllable even when no slots bind. An apparent key with no further valid heartbeat during enumeration is rejected and recovery continues. The provisioning, response timing and four-bulb power-loss/reboot behavior have not been hardware-validated by the software tests.

### Persistence and runtime fallback

Preferences normally survive firmware/OTA updates, **not a full flash erase**. Keep the controller `id` unchanged: it namespaces the record. Erasing flash or changing the ID loses the saved random key and mappings; preserve the one-time INFO discovery lines (`light_id` and eight-hex key) before doing so. App-based re-pairing can change the bulbs' key independently. Saved confirmed bulbs are never re-keyed automatically. Power-cycling other app-keyed bulbs into an active WAKE window explicitly allows reclaiming them with a new key. Mixed meshes need separate controllers or manual setup.

`fastcon.set_mesh_key` validates an eight-hex-character key, saves/applies it, drops queued old-key commands, and restarts low-ID enumeration for an auto controller without sending SET_KEY. It also accepts a lambda. For example, add a native API action to accept a recovered key from Home Assistant:

```yaml
api:
  actions:
    - action: set_fastcon_mesh_key
      variables:
        mesh_key: string
      then:
        - fastcon.set_mesh_key:
            mesh_key: !lambda 'return mesh_key;'
```

The API action appears in Home Assistant's device actions (enable user-defined actions in the ESPHome integration if needed). Call it with the key from ADB. Alternatively a template button can run `fastcon.set_mesh_key: {mesh_key: "31323334"}`. Invalid runtime strings leave the current key untouched. Applying a key does **not** install that key into bulbs; it must already match them. Slot bindings are cleared and re-enumerated, so check their physical identities after switching meshes.

In explicit-key configurations, runtime overrides persist when the action is configured and the compiled key is unchanged. Reflashing with a different explicit key takes precedence; configurations without this new action retain the original explicit-key boot/control behavior.

### Software tests

Run `bash tests/run_native_tests.sh` for codec vectors, the discovery state machine with ESPHome/BLE test adapters, and discovery-disabled C++ syntax checks. Use `FASTCON_SANITIZE=1 bash tests/run_native_tests.sh` to exercise the same native tests under AddressSanitizer/UBSan. With ESPHome installed, run `python tests/test_config.py` and `esphome config tests/test-arduino-discovery.yaml` (also the ESP-IDF twin and existing explicit-key configurations). CI cross-compiles all four configs against stable and beta ESPHome. These checks do not exercise an ESP32 radio, real NVS or physical bulbs.

## Finding Your Mesh Key (Fallback)

The mesh key is crucial for controlling your Fastcon BLE lights. To find your light's mesh key, you first need to setup your devices using an Android device. The app generates a unique mesh key that will be used with all lights that are set up in the app.

Once the lights are setup, you can use ADB to connect to your phone and you may use the following command to extract the mesh key.

```bash
adb logcat | { grep -m 1 -o 'jyq_helper: .* payload:.\{24\},[[:space:]]*key:[[:space:]]*.\{8\}' | awk '{print $NF}'; kill -2 $(pgrep -P $$ adb); }
```

or on Windows (PowerShell):

```powershell
adb logcat | Select-String -Pattern 'jyq_helper: .* payload:.{24},\s*key:\s*.{8}' | ForEach-Object {
    if ($_ -match 'key:\s*(\S{8})') { $matches[1] }
} | Select-Object -First 1
```

While running the above, open the app and toggle a light on and off. The command should then output your mesh key. If it keeps running after printing the key, press Ctrl+C.

If more than one device is connected to ADB, add `-s <device>` after `adb` (see `adb devices`).

Use the key exactly as printed: it's already in the hex format `mesh_key` expects.

## Troubleshooting

**Bulbs don't respond**, or only respond some of the time:

- **Remove `esp32_ble_tracker:`** unless another component needs it. By default it scans continuously, which takes radio time away from advertising, so bulbs miss most commands. If you do need it, set `scan_parameters: continuous: false`.
- Check the log for `fastcon.controller` warnings. A warning there means the Bluetooth controller rejected a command, which the log otherwise wouldn't show.
- Give each command more airtime: raise `adv_duration` (try 100-200), set `repeat_count: 2`, or set `tx_power: 9`.
- Make sure the brMesh app still controls the bulbs, and that the mesh key is the one the app uses now. Resetting or re-pairing bulbs can create a new mesh.

**Color temperature or white does nothing.** The bulbs may not have those LEDs. If the brMesh app only shows a color wheel (plus at most one "white" preset), set `supports_color_temperature: false`. If it has no white at all, set `supports_white: false`. See [Fastcon Light](#fastcon-light).

**Which `light_id` is which bulb?** Lights are usually numbered 1, 2, 3, … in the order they were added in the brMesh app. Turning each one on from ESPHome is the quickest way to check.

**An entity shows a different color than the bulb.** Bulbs don't report their state, so Home Assistant shows what ESPHome last sent. A color changed in the brMesh app won't appear in Home Assistant.

## Acknowledgments

This component builds upon the reverse engineering and hard work of several others who must be acknowledged and thanked:

### Protocol Reverse Engineering

The foundational protocol reverse engineering work was done by [Mooody](https://mooody.me/posts/2023-04/reverse-the-fastcon-ble-protocol/), who provided detailed analysis of the Fastcon BLE protocol, including packet structure and encryption methods. https://mooody.me/posts/2023-04/reverse-the-fastcon-ble-protocol/

The group control, set-group and built-in effect frame formats were decoded by [leon101noel](https://github.com/leon101noel), and the group format was independently confirmed by [oriolfda](https://github.com/oriolfda/esphome-fastcon).

### Contributors

Thank you to everyone who kept this project going with pull requests, forks and bug reports:

- [leon101noel](https://github.com/leon101noel): group control, the bulbs' built-in effects, the plain turn-on and white mode fixes, and the one-command-per-light queue with `repeat_count` (#12, #13, #14)
- [pascal1404](https://github.com/pascal1404): the compile fix for newer ESPHome versions (#8)
- [codeclinic](https://github.com/codeclinic): the Windows mesh key instructions (#5), and detailed bug reports and testing
- [oriolfda](https://github.com/oriolfda/esphome-fastcon): an independent group control implementation that confirmed the frame format
- [brownnath2000](https://github.com/brownnath2000/esphome-fastcon-repeat): per-light color capability flags, the basis for `supports_rgb` / `supports_white`
- [hoogiesngrinderz](https://github.com/hoogiesngrinderz/esphome-fastcon-repeat): sending commands more than once for bulbs that miss them
- [zentik-fr](https://github.com/zentik-fr/esphome-brmesh): research into effects and the white mode payload
- [tofuweasel](https://github.com/tofuweasel/esphome-fastcon), building on [scross01](https://github.com/scross01/esphome-fastcon) and [brett-todd](https://github.com/brett-todd/esphome-fastcon): a fork that kept builds working for many users, plus native pairing work

### Implementation References

- [ArcadeMachinist's brMeshMQTT](https://github.com/ArcadeMachinist/brMeshMQTT) - This work was crucial in helping me understand the practical implementation details of the protocol. https://github.com/ArcadeMachinist/brMeshMQTT

### Discovery References

- **Primary byte-level reference: [AdysTech/br-mesh-controller](https://github.com/AdysTech/br-mesh-controller), especially [protocol.md](https://github.com/AdysTech/br-mesh-controller/blob/main/protocol.md).** Its physical three-bulb captures establish the factory identity layout, WAKE/SET_KEY bytes and boot-window reclaim flow. The discovery codec is cross-checked byte-for-byte against that implementation; its persistent per-bulb architecture also informed this port.
- [dsclee1/BRmesh-esp32-mqtt](https://github.com/dsclee1/BRmesh-esp32-mqtt): generic ESP32 factory scanning, assigning successive light IDs and installing a shared key
- [Zoft123/ha-esp-ble-bridge](https://github.com/Zoft123/ha-esp-ble-bridge): ESPHome-side pairing flow, announced device keys and heartbeat decoding
- [GregoryIvo/brmesh-esphome](https://github.com/GregoryIvo/brmesh-esphome): pairing address pools, persistence and app-free gateway experiments; its documentation distinguishes tested control from unverified gateway pairing
- [Jueff](https://github.com/Jueff) and the [community discussion](https://community.home-assistant.io/t/brmesh-app-bluetooth-lights/473486/95): earlier BRmesh interoperability and key-recovery work

### Community Resources

- [Home Assistant Community Thread](https://community.home-assistant.io/t/brmesh-app-bluetooth-lights/473486/102)

This ESPHome component adapts and/or takes heavy inspiration from all of these works to run directly on ESP32 devices, allowing for native integration with Home Assistant without requiring additional bridges or MQTT brokers. A huge thank you to all those who contributed to my understanding of the Fastcon BLE protocol.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
