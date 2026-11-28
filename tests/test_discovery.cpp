#include <cassert>
#include <iostream>
#include "fastcon_controller.h"
#include "fastcon_light.h"
#include "protocol.h"

using namespace esphome;
using namespace esphome::fastcon;

class TestController : public FastconController
{
public:
    using FastconController::discovery_;
    using FastconController::mesh_key_;
    using FastconController::probe_id_;
    using FastconController::discovery_io_;
    using FastconController::DiscoveryIO;
    using FastconController::send_discovery_round_;
    using FastconController::finish_discovery_round_;
    using FastconController::save_discovery_;
    using FastconController::queue_;
    using FastconController::discovery_failed_;
    using FastconController::failure_reported_;
    using FastconController::mesh_key_confirmed_;
    using FastconController::repair_seen_;
    using FastconController::repair_attempts_;
    using FastconController::discovery_stopped_;
};

static std::vector<uint8_t> heartbeat(const std::array<uint8_t, 4> &key, uint8_t id)
{
    std::vector<uint8_t> frame{0x30, 1, key[3], 0, 0x34, id, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t checksum = 0;
    for (auto byte : frame) checksum += byte;
    frame[3] = checksum;
    for (size_t i = 0; i < frame.size(); i++)
        frame[i] ^= i < 4 ? DEFAULT_ENCRYPT_KEY[i] : key[(i - 4) & 3];
    return frame;
}

static void receive(TestController &controller, const std::vector<uint8_t> &frame, uint64_t mac = 1)
{
    esp32_ble_tracker::ESPBTDevice device;
    device.mac = mac;
    device.manufacturers.push_back({{0xfff0}, frame});
    controller.parse_device(device);
}

static void round(TestController &controller, esp32_ble_tracker::ESP32BLETracker &tracker,
                  const std::vector<uint8_t> &reply = {})
{
    bool scanned = false;
    for (int i = 0; i < 400; i++)
    {
        test_now += 10;
        controller.loop();
        tracker.tick(controller);
        if (controller.discovery_io_ == TestController::DiscoveryIO::SCAN)
        {
            scanned = true;
            if (!reply.empty()) receive(controller, reply);
        }
        if (controller.discovery_io_ == TestController::DiscoveryIO::STOPPING && !tracker.scanning)
            controller.on_scan_end();
        if (controller.ready() || (scanned && controller.discovery_io_ == TestController::DiscoveryIO::REST))
            return;
    }
    assert(false && "nonblocking scan round did not finish");
}

static void prepare(TestController &controller, esp32_ble_tracker::ESP32BLETracker &tracker, uint8_t max = 4,
                    uint32_t timeout = 30)
{
    controller.set_preference_key(1);
    controller.set_discovery(&tracker, max, timeout);
    controller.setup();
}

int main()
{
    test_preferences.clear();
    TestController waiting;
    esp32_ble_tracker::ESP32BLETracker waiting_tracker;
    prepare(waiting, waiting_tracker);
    esp32_ble::test_ble.active = false;
    test_now += 10000;
    waiting.loop();
    assert(test_advertisements.empty() && waiting_tracker.starts == 0);
    esp32_ble::test_ble.active = true;
    test_preferences.clear();
    TestController controller;
    esp32_ble_tracker::ESP32BLETracker tracker;
    FastconLight slot(0), explicit_light(1);
    slot.set_slot(1);
    slot.set_controller(&controller);
    controller.register_light(&slot);
    controller.register_light(&explicit_light);
    prepare(controller, tracker, 1);
    assert(!controller.ready() && slot.get_light_id() == 0 && slot.warning);
    const auto key = controller.mesh_key_;
    std::vector<uint8_t> factory{0x4e, 1, 2, 3, 0xe8, 0x70, 0x72, 0xf2, 0xe7, 0x70,
                                 0x2a, 0xa8, 0x5e, 0x36, 0x7b, 0xc4};
    round(controller, tracker, factory);
    assert(controller.discovery_.count == 1);
    assert(controller.discovery_.bulbs[0].light_id == 2); // explicit address is never stolen
    assert(!controller.discovery_.bulbs[0].claimed && slot.get_light_id() == 0);
    DiscoveryRecord saved;
    auto pref = global_preferences->make_preference<DiscoveryRecord>(1, true);
    assert(pref.load(&saved) && saved.key == key && saved.bulbs[0].light_id == 2);
    round(controller, tracker, heartbeat(key, 2));
    assert(controller.ready() && slot.get_light_id() == 2 && !slot.warning);
    assert(controller.discovery_.bulbs[0].claimed);
    assert(test_advertisements.front()[3] == 27);
    for (const auto &advertisement : test_advertisements)
        assert(advertisement.size() == 31);

    // Steady-state control, including the final group ON, uses complete ADs too.
    for (int step = 0; step < 20; step++) {
        test_now += 10;
        controller.loop();
    }
    assert(test_advertisements.back()[3] + 4u == test_advertisements.back().size());
    const auto before_control = test_advertisements.size();
    controller.queueCommand(2, controller.single_control(2, {0xff}));
    for (int step = 0; step < 20; step++) {
        test_now += 10;
        controller.loop();
    }
    assert(test_advertisements.size() == before_control + 1);
    assert(test_advertisements.back()[3] == 27);

    // A legacy controller in a discovery-enabled build keeps its historical AD.
    TestController legacy;
    legacy.set_mesh_key(key);
    legacy.setup();
    legacy.queueCommand(1, legacy.single_control(1, {0xff}));
    legacy.loop();
    assert(test_advertisements.back()[3] == 26);

    TestController restored;
    esp32_ble_tracker::ESP32BLETracker restored_tracker;
    prepare(restored, restored_tracker, 1);
    assert(restored.ready() && restored.mesh_key_ == key);
    restored.loop();
    assert(restored_tracker.starts == 0 && restored.is_queue_empty());

    // Saving an unconfirmed SET_KEY is sufficient to recover after power loss.
    restored.discovery_.phase = DiscoveryPhase::FACTORY;
    restored.discovery_.bulbs[0].claimed = 0;
    assert(restored.save_discovery_());
    TestController pending;
    esp32_ble_tracker::ESP32BLETracker pending_tracker;
    prepare(pending, pending_tracker, 4, 120);
    assert(pending.mesh_key_ == key && pending.discovery_.count == 1);
    assert(!pending.discovery_.bulbs[0].claimed && !pending.mesh_key_confirmed_);
    // A late duplicate cannot renew the original deadline.
    pending.loop(); // Initialize the radio deadline before advancing simulated time.
    test_now += 119000;
    round(pending, pending_tracker, factory);
    test_now += 2000;
    pending.loop();
    assert(pending.discovery_stopped_ && !pending.discovery_.bulbs[0].claimed);

    // Partial factory confirmation remains resumable after timeout and reboot.
    test_preferences.clear();
    TestController partial;
    esp32_ble_tracker::ESP32BLETracker partial_tracker;
    prepare(partial, partial_tracker, 2);
    partial.set_discovery(&partial_tracker, 2, 1);
    auto second_factory = factory;
    second_factory[4]++;
    partial.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(partial, factory);
    receive(partial, second_factory);
    receive(partial, heartbeat(partial.mesh_key_, 1));
    const auto confirmed = partial.discovery_.bulbs[0];
    test_now += 2000;
    partial.finish_discovery_round_();
    assert(pref.load(&saved) && saved.phase == DiscoveryPhase::FACTORY);
    assert(saved.bulbs[0].claimed && !saved.bulbs[1].claimed);
    assert(partial.ready());
    partial.discovery_io_ = TestController::DiscoveryIO::REST;
    for (int step = 0; step < 100; step++) { test_now += 10; partial.loop(); }
    assert(partial_tracker.starts == 0); // bounded stop, not continuous retries
    TestController partial_reboot;
    esp32_ble_tracker::ESP32BLETracker partial_reboot_tracker;
    prepare(partial_reboot, partial_reboot_tracker, 2);
    assert(partial_reboot.discovery_.phase == DiscoveryPhase::FACTORY);
    assert(partial_reboot.mesh_key_ == partial.mesh_key_);
    // Even a factory identity for a confirmed bulb must never trigger SET_KEY.
    partial_reboot.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(partial_reboot, factory);
    partial_reboot.discovery_io_ = TestController::DiscoveryIO::SEND;
    for (int step = 0; step < 4; step++) {
        partial_reboot.clear_queue();
        partial_reboot.send_discovery_round_();
        const auto &packet = partial_reboot.queue_.front().data;
        std::vector<uint8_t> rf(15, 0);
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        assert((((rf[21] ^ DEFAULT_ENCRYPT_KEY[0]) >> 4) & 7) != 2);
    }
    partial_reboot.clear_queue();
    round(partial_reboot, partial_reboot_tracker, heartbeat(partial.mesh_key_, 2));
    assert(partial_reboot.ready() && partial_reboot.discovery_.phase == DiscoveryPhase::DONE);
    assert(partial_reboot.discovery_.bulbs[1].claimed);
    assert(partial_reboot.discovery_.bulbs[0].did == confirmed.did);
    assert(partial_reboot.discovery_.bulbs[0].light_id == confirmed.light_id);
    assert(partial_reboot.discovery_.bulbs[0].claimed == confirmed.claimed);

    // A resumed enumeration must not duplicate previously confirmed addresses.
    pending.discovery_.phase = DiscoveryPhase::ENUMERATE;
    pending.discovery_.count = 1;
    pending.discovery_.bulbs[0].light_id = 1;
    pending.discovery_.bulbs[0].claimed = 1;
    pending.probe_id_ = 1;
    pending.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(pending, heartbeat(key, 1));
    assert(pending.discovery_.count == 1);

    // Only one queued command per recovery candidate, even with repeat_count: 5.
    pending.clear_queue();
    pending.set_repeat_count(5);
    pending.discovery_.count = 0;
    pending.discovery_.phase = DiscoveryPhase::RECOVERY;
    pending.discovery_.candidate = 1;
    pending.send_discovery_round_();
    assert(pending.queue_.size() == 1 && pending.queue_[0].sends_left == 1);
    pending.clear_queue();
    for (int i = 0; i < 49; i++) pending.finish_discovery_round_();
    assert(pending.discovery_.candidate == 50);
    assert(pref.load(&saved) && saved.candidate == 50);
    pending.discovery_.candidate = 9999;
    pending.finish_discovery_round_();
    assert(pending.discovery_.phase == DiscoveryPhase::FAILED);
    assert(!pending.ready());

    // An exhausted checkpoint must never wrap around to key 0000 on reboot.
    pending.discovery_.phase = DiscoveryPhase::RECOVERY;
    pending.discovery_.candidate = 10000;
    assert(pending.save_discovery_());
    TestController exhausted;
    esp32_ble_tracker::ESP32BLETracker exhausted_tracker;
    prepare(exhausted, exhausted_tracker);
    assert(exhausted.discovery_.phase == DiscoveryPhase::FAILED);

    assert(!exhausted.apply_mesh_key("invalid!"));
    assert(exhausted.apply_mesh_key("31323334"));
    assert(exhausted.mesh_key_ == app_mesh_key(1234));
    assert(exhausted.discovery_.phase == DiscoveryPhase::ENUMERATE);
    assert(pref.load(&saved) && saved.runtime_override && saved.key == app_mesh_key(1234));

    // Replacing a key during a scan must restart at ID 1, not finish an old round.
    exhausted.discovery_io_ = TestController::DiscoveryIO::SCAN;
    assert(exhausted.apply_mesh_key("31323334"));
    exhausted.on_scan_end();
    exhausted.loop();
    exhausted.loop();
    assert(exhausted.probe_id_ == 1);

    test_storage_ok = false;
    assert(!exhausted.apply_mesh_key("31323335"));
    assert(exhausted.mesh_key_ == app_mesh_key(1234));
    test_storage_ok = true;

    // Flashing a new explicit key must win over an older runtime override.
    TestController reflashed;
    reflashed.set_mesh_key(app_mesh_key(5678));
    reflashed.set_preference_key(1);
    reflashed.setup();
    assert(reflashed.mesh_key_ == app_mesh_key(5678));

    // A radio API failure is not a tested key: do not skip it silently.
    TestController radio_failure;
    esp32_ble_tracker::ESP32BLETracker failure_tracker;
    prepare(radio_failure, failure_tracker);
    radio_failure.discovery_.phase = DiscoveryPhase::RECOVERY;
    radio_failure.discovery_.candidate = 100;
    radio_failure.discovery_io_ = TestController::DiscoveryIO::SEND;
    test_advertise_result = 1;
    for (int i = 0; i < 100; i++) {
        test_now += 10;
        radio_failure.loop();
        if (radio_failure.discovery_io_ == TestController::DiscoveryIO::STOPPING) radio_failure.on_scan_end();
    }
    test_advertise_result = ESP_OK;
    assert(radio_failure.discovery_.candidate == 100 && radio_failure.discovery_failed_);

    // Async scan failures are cleanup callbacks, not completed listen windows.
    for (bool fail_start : {true, false}) {
        test_preferences.clear();
        TestController scanner_failure;
        esp32_ble_tracker::ESP32BLETracker scanner_failure_tracker;
        prepare(scanner_failure, scanner_failure_tracker);
        scanner_failure.discovery_.phase = DiscoveryPhase::RECOVERY;
        scanner_failure.discovery_.candidate = 149; // next value would checkpoint
        assert(scanner_failure.save_discovery_());
        scanner_failure_tracker.fail_start = fail_start;
        scanner_failure_tracker.fail_stop = !fail_start;
        for (int step = 0; step < 500; step++) {
            test_now += 10;
            scanner_failure.loop();
            scanner_failure_tracker.tick(scanner_failure);
        }
        assert(scanner_failure_tracker.starts == 1);
        assert(scanner_failure.discovery_.candidate == 149 && scanner_failure.discovery_failed_);
        assert(pref.load(&saved) && saved.candidate == 149);
        TestController scanner_reboot;
        esp32_ble_tracker::ESP32BLETracker scanner_reboot_tracker;
        prepare(scanner_reboot, scanner_reboot_tracker);
        assert(scanner_reboot.discovery_.candidate == 149);
        round(scanner_reboot, scanner_reboot_tracker);
        assert(scanner_reboot.discovery_.candidate == 150);
    }

    // Repeated failures retain both in-memory and persisted identities and candidate.
    test_preferences.clear();
    TestController retrying;
    esp32_ble_tracker::ESP32BLETracker retry_tracker;
    prepare(retrying, retry_tracker);
    retrying.discovery_.phase = DiscoveryPhase::RECOVERY;
    retrying.discovery_.candidate = 149;
    retrying.discovery_.count = 2;
    retrying.discovery_.bulbs[0].did = {1, 2, 3, 4, 5, 6};
    retrying.discovery_.bulbs[0].light_id = 1;
    retrying.discovery_.bulbs[0].claimed = 1;
    retrying.discovery_.bulbs[1].did = {6, 5, 4, 3, 2, 1};
    retrying.discovery_.bulbs[1].light_id = 2;
    const auto retry_key = retrying.mesh_key_;
    assert(retrying.save_discovery_());
    retry_tracker.fail_start = true;
    for (int step = 0; step < 12000; step++) {
        test_now += 10;
        retrying.loop();
        retry_tracker.tick(retrying);
        assert(retrying.discovery_.candidate == 149);
    }
    assert(retry_tracker.starts >= 10);
    assert(retrying.mesh_key_ == retry_key && retrying.discovery_.count == 2);
    assert(pref.load(&saved) && saved.candidate == 149 && saved.count == 2);
    assert(saved.bulbs[0].did == retrying.discovery_.bulbs[0].did && saved.bulbs[0].claimed);
    assert(saved.bulbs[1].did == retrying.discovery_.bulbs[1].did && !saved.bulbs[1].claimed);

    // The factory deadline is wall-clock time even when all scan starts fail.
    test_preferences.clear();
    TestController timeout_failure;
    esp32_ble_tracker::ESP32BLETracker timeout_tracker;
    prepare(timeout_failure, timeout_tracker);
    timeout_failure.set_discovery(&timeout_tracker, 4, 1);
    timeout_tracker.fail_start = true;
    const auto timeout_start = test_now;
    while (test_now - timeout_start < 1000) {
        test_now += 10;
        timeout_failure.loop();
        timeout_tracker.tick(timeout_failure);
    }
    assert(timeout_failure.discovery_.phase == DiscoveryPhase::FACTORY);
    test_now += 10;
    timeout_failure.loop();
    timeout_tracker.tick(timeout_failure);
    assert(timeout_failure.discovery_.phase == DiscoveryPhase::RECOVERY);
    timeout_tracker.fail_start = false;
    for (int step = 0; step < 2000 && timeout_failure.discovery_.candidate == 0; step++) {
        test_now += 10;
        timeout_failure.loop();
        timeout_tracker.tick(timeout_failure);
    }
    assert(timeout_failure.discovery_.candidate > 0);

    // Expiring in the middle of a valid factory scan must not consume key 0.
    test_preferences.clear();
    TestController crossing;
    esp32_ble_tracker::ESP32BLETracker crossing_tracker;
    prepare(crossing, crossing_tracker);
    crossing.set_discovery(&crossing_tracker, 4, 1);
    while (!crossing_tracker.scanning) {
        test_now += 10;
        crossing.loop();
        crossing_tracker.tick(crossing);
    }
    test_now += 1000;
    crossing.loop();
    crossing_tracker.tick(crossing);
    assert(crossing.discovery_.phase == DiscoveryPhase::RECOVERY);
    for (int step = 0; step < 50; step++) {
        test_now += 10;
        crossing.loop();
        crossing_tracker.tick(crossing);
        if (crossing.discovery_io_ == TestController::DiscoveryIO::REST) break;
    }
    assert(crossing.discovery_.candidate == 0);

    // A pending identity at the deadline stays pending, including on scanner failure.
    test_preferences.clear();
    TestController pending_timeout;
    esp32_ble_tracker::ESP32BLETracker pending_timeout_tracker;
    prepare(pending_timeout, pending_timeout_tracker);
    pending_timeout.set_discovery(&pending_timeout_tracker, 4, 1);
    pending_timeout.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(pending_timeout, factory);
    pending_timeout.discovery_io_ = TestController::DiscoveryIO::SEND;
    pending_timeout_tracker.fail_start = true;
    for (int step = 0; step < 2500; step++) {
        test_now += 10;
        pending_timeout.loop();
        pending_timeout_tracker.tick(pending_timeout);
    }
    assert(pending_timeout.discovery_.phase == DiscoveryPhase::FACTORY);
    assert(pending_timeout.discovery_.count == 1 && !pending_timeout.discovery_.bulbs[0].claimed);
    assert(pref.load(&saved) && saved.phase == DiscoveryPhase::FACTORY && saved.count == 1);
    assert(pending_timeout_tracker.starts == 1 && !pending_timeout.ready());

    // One failed scan must not prevent later factory onboarding.
    test_preferences.clear();
    TestController transient;
    esp32_ble_tracker::ESP32BLETracker transient_tracker;
    prepare(transient, transient_tracker, 1);
    transient.set_discovery(&transient_tracker, 1, 30);
    transient_tracker.fail_start = true;
    while (!transient.discovery_failed_) {
        test_now += 10;
        transient.loop();
        transient_tracker.tick(transient);
    }
    transient_tracker.fail_start = false;
    for (int step = 0; step < 2000 && transient.discovery_.count == 0; step++) {
        test_now += 10;
        transient.loop();
        transient_tracker.tick(transient);
        if (transient_tracker.scanning) receive(transient, factory);
    }
    assert(transient.discovery_.count == 1);
    round(transient, transient_tracker, heartbeat(transient.mesh_key_, 1));
    assert(transient.ready() && transient.discovery_.bulbs[0].claimed);

    // The listen window starts on confirmed scan-start, not the scan request.
    test_preferences.clear();
    TestController delayed_scan;
    esp32_ble_tracker::ESP32BLETracker delayed_tracker;
    prepare(delayed_scan, delayed_tracker);
    delayed_scan.discovery_.phase = DiscoveryPhase::RECOVERY;
    delayed_scan.discovery_.candidate = 101;
    delayed_tracker.start_delay_ms = 300;
    round(delayed_scan, delayed_tracker);
    assert(delayed_scan.discovery_.candidate == 102 && !delayed_scan.discovery_failed_);
    assert(test_now - delayed_tracker.requested_at >= 500);

    // An early scan-end cannot become a full listen window during later cleanup.
    test_preferences.clear();
    TestController short_scan;
    esp32_ble_tracker::ESP32BLETracker short_tracker;
    prepare(short_scan, short_tracker);
    short_scan.discovery_.phase = DiscoveryPhase::RECOVERY;
    short_scan.discovery_.candidate = 101;
    while (!short_tracker.scanning) {
        test_now += 10;
        short_scan.loop();
        short_tracker.tick(short_scan);
    }
    test_now += 20;
    short_tracker.scanning = false;
    short_scan.on_scan_end();
    test_now += 1000; // application loop stalled after the early end callback
    for (int step = 0; step < 10; step++) {
        short_scan.loop();
        short_tracker.tick(short_scan);
        test_now += 10;
    }
    assert(short_scan.discovery_failed_ && short_scan.discovery_.candidate == 101);

    // Boot restoration bookkeeping is independent of transport readiness.
    test_preferences.clear();
    TestController light_boot;
    esp32_ble_tracker::ESP32BLETracker light_boot_tracker;
    prepare(light_boot, light_boot_tracker);
    FastconLight boot_group(0), boot_single(1);
    boot_group.set_group_id(0);
    boot_group.set_controller(&light_boot);
    boot_single.set_controller(&light_boot);
    light::LightState group_state, single_state;
    group_state.output = &boot_group;
    single_state.output = &boot_single;
    boot_group.setup_state(&group_state);
    boot_single.setup_state(&single_state);
    single_state.current_values.green = 0;
    single_state.current_values.blue = 0;
    boot_group.write_state(&group_state);
    boot_single.write_state(&single_state);
    assert(light_boot.is_queue_empty());
    light_boot.discovery_.phase = DiscoveryPhase::DONE;
    group_state.current_values.state = true;
    group_state.remote_values = group_state.current_values;
    boot_group.write_state(&group_state);
    assert(light_boot.get_queue_size() == 1); // the FIRST post-discovery request
    light_boot.clear_queue();
    single_state.current_values.state = true;
    single_state.current_values.red = 0;
    single_state.current_values.blue = 1;
    single_state.remote_values = single_state.current_values;
    boot_single.write_state(&single_state);
    assert(light_boot.get_queue_size() == 1);
    std::vector<uint8_t> single_rf(15, 0);
    const auto &single_packet = light_boot.queue_.front().data;
    single_rf.insert(single_rf.end(), single_packet.begin(), single_packet.end());
    WhiteningContext single_ctx;
    whitening_init(0x25, single_ctx);
    whitening_encode(single_rf, single_ctx);
    assert((single_rf[25] ^ light_boot.mesh_key_[0]) == 0x72); // new color, not short ON

    // Reserved addresses confirm the mesh key without consuming discovery slots.
    for (bool runtime_override : {false, true}) {
        test_preferences.clear();
        TestController reserved;
        esp32_ble_tracker::ESP32BLETracker reserved_tracker;
        FastconLight reserved_light(1), unbound_slot(0);
        reserved_light.set_controller(&reserved);
        unbound_slot.set_controller(&reserved);
        unbound_slot.set_slot(1);
        reserved.register_light(&reserved_light);
        reserved.register_light(&unbound_slot);
        prepare(reserved, reserved_tracker);
        light::LightState reserved_state;
        reserved_light.setup_state(&reserved_state);
        reserved_light.write_state(&reserved_state); // boot restoration during discovery
        const auto recovered_key = app_mesh_key(1234);
        if (runtime_override)
            assert(reserved.apply_mesh_key("31323334"));
        else {
            reserved.discovery_.phase = DiscoveryPhase::RECOVERY;
            reserved.discovery_.candidate = 1234;
            round(reserved, reserved_tracker, heartbeat(recovered_key, 1));
            assert(reserved.discovery_.phase == DiscoveryPhase::ENUMERATE);
        }
        for (int step = 0; step < 16; step++)
            round(reserved, reserved_tracker, heartbeat(recovered_key, 1));
        assert(reserved.discovery_.count == 0 && unbound_slot.get_light_id() == 0 && unbound_slot.warning);
        assert(reserved.discovery_.phase == DiscoveryPhase::DONE && reserved.ready());
        reserved.clear_queue();
        reserved_state.current_values.state = true;
        reserved_state.remote_values = reserved_state.current_values;
        reserved_light.write_state(&reserved_state);
        assert(reserved.get_queue_size() == 1); // explicit entities remain usable
        TestController reserved_reboot;
        esp32_ble_tracker::ESP32BLETracker reserved_reboot_tracker;
        prepare(reserved_reboot, reserved_reboot_tracker);
        assert(reserved_reboot.ready() && reserved_reboot.discovery_.count == 0);
        assert(reserved_reboot.mesh_key_ == recovered_key);
    }

    // A failed first confirmation rolls back the enumeration insertion, so a
    // later valid reply after storage recovery can claim and persist the slot.
    test_preferences.clear();
    TestController write_retry;
    esp32_ble_tracker::ESP32BLETracker write_retry_tracker;
    FastconLight write_retry_slot(0);
    write_retry_slot.set_slot(1);
    write_retry_slot.set_controller(&write_retry);
    write_retry.register_light(&write_retry_slot);
    prepare(write_retry, write_retry_tracker);
    assert(write_retry.apply_mesh_key("31323334"));
    const auto write_retry_key = write_retry.mesh_key_;
    write_retry.discovery_io_ = TestController::DiscoveryIO::SCAN;
    test_storage_ok = false;
    receive(write_retry, heartbeat(write_retry_key, 1));
    assert(write_retry.discovery_failed_ && write_retry.discovery_.count == 0);
    test_storage_ok = true;
    // Model the controller's normal storage-recovery retry boundary.
    write_retry.discovery_failed_ = false;
    write_retry.failure_reported_ = false;
    write_retry.discovery_io_ = TestController::DiscoveryIO::SCAN;
    write_retry.send_discovery_round_(); // next round resets the per-round heartbeat guard
    receive(write_retry, heartbeat(write_retry_key, 1));
    receive(write_retry, heartbeat(write_retry_key, 1));
    assert(write_retry.discovery_.count == 1 && write_retry.discovery_.bulbs[0].claimed);
    assert(write_retry_slot.get_light_id() == 1);
    assert(pref.load(&saved) && saved.count == 1 && saved.bulbs[0].claimed);
    for (int probe = 0; probe < 16; probe++) write_retry.finish_discovery_round_();
    assert(write_retry.ready());
    TestController write_retry_reboot;
    esp32_ble_tracker::ESP32BLETracker write_retry_reboot_tracker;
    FastconLight write_retry_reboot_slot(0);
    write_retry_reboot_slot.set_slot(1);
    write_retry_reboot_slot.set_controller(&write_retry_reboot);
    write_retry_reboot.register_light(&write_retry_reboot_slot);
    prepare(write_retry_reboot, write_retry_reboot_tracker);
    assert(write_retry_reboot.ready() && write_retry_reboot_slot.get_light_id() == 1);

    // A delayed ID-2 heartbeat during probe 1 must be retained and persisted;
    // finishing the remaining probes may not leave DONE with zero claimed slots.
    test_preferences.clear();
    TestController delayed_id;
    esp32_ble_tracker::ESP32BLETracker delayed_id_tracker;
    FastconLight delayed_slot(0);
    delayed_slot.set_slot(1);
    delayed_slot.set_controller(&delayed_id);
    delayed_id.register_light(&delayed_slot);
    prepare(delayed_id, delayed_id_tracker);
    assert(delayed_id.apply_mesh_key("31323334"));
    delayed_id.discovery_io_ = TestController::DiscoveryIO::SCAN;
    delayed_id.probe_id_ = 1;
    receive(delayed_id, heartbeat(delayed_id.mesh_key_, 2));
    assert(delayed_id.discovery_.count == 1 && delayed_id.discovery_.bulbs[0].light_id == 2);
    assert(delayed_id.discovery_.bulbs[0].claimed && delayed_slot.get_light_id() == 2);
    assert(pref.load(&saved) && saved.bulbs[0].light_id == 2 && saved.bulbs[0].claimed);
    for (int probe = 0; probe < 16; probe++) delayed_id.finish_discovery_round_();
    assert(delayed_id.discovery_.phase == DiscoveryPhase::DONE);
    assert(delayed_slot.get_light_id() == 2);
    TestController delayed_reboot;
    esp32_ble_tracker::ESP32BLETracker delayed_reboot_tracker;
    FastconLight delayed_reboot_slot(0);
    delayed_reboot_slot.set_slot(1);
    delayed_reboot_slot.set_controller(&delayed_reboot);
    delayed_reboot.register_light(&delayed_reboot_slot);
    prepare(delayed_reboot, delayed_reboot_tracker);
    assert(delayed_reboot.ready() && delayed_reboot_slot.get_light_id() == 2);

    // A bulb installs the first SET_KEY but drops its unsolicited confirmation.
    // It responds only to a command addressed to its assigned ID under that key.
    test_preferences.clear();
    test_advertisements.clear();
    TestController lost_confirmation;
    esp32_ble_tracker::ESP32BLETracker lost_tracker;
    FastconLight lost_slot(0);
    lost_slot.set_slot(1);
    lost_slot.set_controller(&lost_confirmation);
    lost_confirmation.register_light(&lost_slot);
    prepare(lost_confirmation, lost_tracker, 1, 30);
    const auto original_key = lost_confirmation.mesh_key_;
    bool identity_sent = false, installed = false, addressed = false;
    uint8_t assigned = 0;
    size_t ad_index = 0;
    const auto lost_start = test_now;
    for (int step = 0; step < 3000 && !lost_confirmation.ready(); step++) {
        test_now += 10;
        lost_confirmation.loop();
        lost_tracker.tick(lost_confirmation);
        while (ad_index < test_advertisements.size()) {
            const auto &ad = test_advertisements[ad_index++];
            assert(ad.size() == 31 && ad[3] == 27);
            std::vector<uint8_t> rf(15, 0);
            rf.insert(rf.end(), ad.begin() + 7, ad.end());
            WhiteningContext ctx;
            whitening_init(0x25, ctx);
            whitening_encode(rf, ctx);
            for (size_t i = 21; i < 25; i++) rf[i] ^= DEFAULT_ENCRYPT_KEY[i - 21];
            const auto type = (rf[21] >> 4) & 7;
            const auto &key = type == 2 ? DEFAULT_ENCRYPT_KEY : original_key;
            for (size_t i = 25; i < 37; i++) rf[i] ^= key[(i - 25) & 3];
            if (type == 2 && !installed) {
                assert(std::equal(rf.begin() + 25, rf.begin() + 31, factory.begin() + 4));
                assigned = rf[31];
                assert(assigned == 1 && rf[32] == 1);
                assert(std::equal(rf.begin() + 33, rf.begin() + 37, original_key.begin()));
                installed = true;
            } else if (type == 5 && installed && rf[25] == 0x22 && rf[26] == assigned &&
                       rf[23] == original_key[3]) {
                uint8_t checksum = 0;
                for (size_t i = 21; i < 37; i++) if (i != 24) checksum += rf[i];
                assert(checksum == rf[24]);
                addressed = true;
            }
        }
        if (lost_tracker.scanning) {
            if (!identity_sent) {
                receive(lost_confirmation, factory);
                identity_sent = true;
            } else if (addressed) {
                receive(lost_confirmation, heartbeat(original_key, assigned));
            }
        }
    }
    assert(test_now - lost_start < 30000);
    assert(installed && addressed && lost_confirmation.ready());
    assert(lost_confirmation.mesh_key_ == original_key && lost_slot.get_light_id() == assigned);
    assert(lost_confirmation.discovery_.bulbs[0].claimed);
    assert(pref.load(&saved) && saved.key == original_key && saved.bulbs[0].light_id == assigned && saved.bulbs[0].claimed);

    // A restored pending bulb identifies itself by its saved advertiser MAC but
    // reports a different number. Only a same-key heartbeat may schedule repair.
    test_preferences.clear();
    TestController repair_seed;
    esp32_ble_tracker::ESP32BLETracker repair_seed_tracker;
    prepare(repair_seed, repair_seed_tracker, 1, 120);
    repair_seed.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(repair_seed, factory, 0x112233445566ULL);
    const auto repair_key = repair_seed.mesh_key_;
    TestController repair;
    esp32_ble_tracker::ESP32BLETracker repair_tracker;
    FastconLight repair_slot(0);
    repair_slot.set_slot(1);
    repair_slot.set_controller(&repair);
    repair.register_light(&repair_slot);
    prepare(repair, repair_tracker, 1, 120);
    repair.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(repair, heartbeat(repair_key, 7), 0x998877665544ULL);
    receive(repair, heartbeat(app_mesh_key(1234), 7), 0x112233445566ULL);
    auto bad_repair_heartbeat = heartbeat(repair_key, 7);
    const uint8_t bad_safe = repair_key[3] == 0x55 ? 0x56 : 0x55;
    const uint8_t old_sum = bad_repair_heartbeat[3] ^ DEFAULT_ENCRYPT_KEY[3];
    bad_repair_heartbeat[2] ^= repair_key[3] ^ bad_safe;
    bad_repair_heartbeat[3] ^= old_sum ^ static_cast<uint8_t>(old_sum - repair_key[3] + bad_safe);
    receive(repair, bad_repair_heartbeat, 0x112233445566ULL);
    assert(!repair.repair_seen_[0]);
    for (int step = 0; step < 4; step++) {
        repair.clear_queue();
        repair.send_discovery_round_();
        std::vector<uint8_t> rf(15, 0);
        const auto &packet = repair.queue_.front().data;
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        assert((((rf[21] ^ DEFAULT_ENCRYPT_KEY[0]) >> 4) & 7) != 2);
    }
    receive(repair, heartbeat(repair_key, 7), 0x112233445566ULL);
    unsigned repair_count = 0, repair_probes = 0;
    for (int step = 0; step < 32; step++) {
        repair.clear_queue();
        repair.send_discovery_round_();
        std::vector<uint8_t> rf(15, 0);
        const auto &packet = repair.queue_.front().data;
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        const auto type = ((rf[21] ^ DEFAULT_ENCRYPT_KEY[0]) >> 4) & 7;
        if (type == 2) {
            repair_count++;
            assert((rf[23] ^ DEFAULT_ENCRYPT_KEY[2]) == repair_key[3]);
            for (size_t i = 25; i < 37; i++) rf[i] ^= repair_key[(i - 25) & 3];
            assert(std::equal(rf.begin() + 25, rf.begin() + 31, factory.begin() + 4));
            assert(rf[31] == 1 && rf[32] == 1);
            assert(std::equal(rf.begin() + 33, rf.begin() + 37, repair_key.begin()));
        } else if (type == 5) repair_probes++;
        receive(repair, heartbeat(repair_key, 7), 0x112233445566ULL);
    }
    assert(repair_count == 3 && repair_probes >= 3);
    receive(repair, heartbeat(repair_key, 1), 0x112233445566ULL);
    assert(repair.discovery_.bulbs[0].claimed && repair_slot.get_light_id() == 1);
    assert(pref.load(&saved) && saved.bulbs[0].claimed && saved.key == repair_key);

    // Shared advertiser MAC cannot attribute a heartbeat to either DID. Both
    // restored pending identities may be repaired, but each packet is DID-addressed
    // and each has its own retry budget.
    test_preferences.clear();
    TestController shared_seed;
    esp32_ble_tracker::ESP32BLETracker shared_seed_tracker;
    prepare(shared_seed, shared_seed_tracker, 2, 120);
    shared_seed.discovery_io_ = TestController::DiscoveryIO::SCAN;
    auto shared_second = factory;
    shared_second[4]++;
    constexpr uint64_t shared_mac = 0x112233445566ULL;
    receive(shared_seed, factory, shared_mac);
    receive(shared_seed, shared_second, shared_mac);
    const auto shared_saved = shared_seed.discovery_;
    assert(shared_saved.count == 2 && shared_saved.bulbs[0].light_id == 1 &&
           shared_saved.bulbs[1].light_id == 2);
    TestController shared_repair;
    esp32_ble_tracker::ESP32BLETracker shared_repair_tracker;
    prepare(shared_repair, shared_repair_tracker, 2, 120);
    shared_repair.loop(); // initialize the factory timeout
    shared_repair.discovery_io_ = TestController::DiscoveryIO::SCAN;
    receive(shared_repair, heartbeat(shared_saved.key, 7), shared_mac);
    assert(shared_repair.repair_seen_[0] && shared_repair.repair_seen_[1]);
    unsigned attempts[2] = {}, probes_after_budget[2] = {};
    for (int step = 0; step < 64; step++) {
        shared_repair.clear_queue();
        shared_repair.send_discovery_round_();
        assert(shared_repair.queue_.size() == 1);
        std::vector<uint8_t> rf(15, 0);
        const auto &packet = shared_repair.queue_.front().data;
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        for (size_t i = 21; i < 25; i++) rf[i] ^= DEFAULT_ENCRYPT_KEY[i - 21];
        const auto type = (rf[21] >> 4) & 7;
        if (type == 2 || type == 5) {
            for (size_t i = 25; i < 37; i++) rf[i] ^= shared_saved.key[(i - 25) & 3];
            if (type == 2) {
                const auto id = rf[31];
                assert(id >= 1 && id <= 2 && rf[32] == 1 && rf[23] == shared_saved.key[3]);
                assert(std::equal(rf.begin() + 25, rf.begin() + 31, shared_saved.bulbs[id - 1].did.begin()));
                assert(std::equal(rf.begin() + 33, rf.begin() + 37, shared_saved.key.begin()));
                assert(++attempts[id - 1] <= 3);
            } else {
                assert(rf[25] == 0x22 && rf[26] >= 1 && rf[26] <= 2);
                if (attempts[rf[26] - 1] == 3) probes_after_budget[rf[26] - 1]++;
            }
        }
        receive(shared_repair, heartbeat(shared_saved.key, 7), shared_mac);
    }
    assert(attempts[0] == 3 && attempts[1] == 3);
    assert(shared_repair.repair_attempts_[0] == 3 && shared_repair.repair_attempts_[1] == 3);
    assert(probes_after_budget[0] > 0 && probes_after_budget[1] > 0);
    assert(shared_repair.mesh_key_ == shared_saved.key && shared_repair.discovery_.count == 2);
    for (int i = 0; i < 2; i++) {
        const auto &bulb = shared_repair.discovery_.bulbs[i];
        assert(!bulb.claimed && bulb.did == shared_saved.bulbs[i].did &&
               bulb.light_id == shared_saved.bulbs[i].light_id && bulb.mac == shared_mac);
    }
    shared_repair.clear_queue();
    shared_repair.discovery_io_ = TestController::DiscoveryIO::REST;
    test_now += 121000;
    shared_repair.loop();
    assert(shared_repair.discovery_stopped_);
    assert(pref.load(&saved) && saved.count == 2 && !saved.bulbs[0].claimed && !saved.bulbs[1].claimed);

    // Duplicate factory identities cannot extend the timeout; pending key and
    // identity remain persisted and radio rounds stop at expiry.
    test_preferences.clear();
    TestController bounded;
    esp32_ble_tracker::ESP32BLETracker bounded_tracker;
    prepare(bounded, bounded_tracker, 1, 1);
    const auto bounded_key = bounded.mesh_key_;
    unsigned bounded_scans = 0;
    for (int step = 0; step < 1000 && !bounded.discovery_stopped_; step++) {
        test_now += 10;
        bounded.loop();
        bounded_tracker.tick(bounded);
        if (bounded_tracker.scanning) {
            bounded_scans++;
            receive(bounded, factory);
        }
        if (bounded.discovery_io_ == TestController::DiscoveryIO::STOPPING && !bounded_tracker.scanning)
            bounded.on_scan_end();
    }
    assert(bounded.discovery_stopped_);
    assert(bounded_tracker.starts < 20);
    assert(bounded.discovery_.phase == DiscoveryPhase::FACTORY && bounded.discovery_.count == 1);
    assert(bounded.discovery_.key == bounded_key && bounded.discovery_.bulbs[0].did[0] != 0);
    assert(pref.load(&saved) && saved.key == bounded_key && saved.count == 1 && !saved.bulbs[0].claimed);
    const auto scans_at_expiry = bounded_scans;
    for (int step = 0; step < 100; step++) {
        test_now += 10;
        bounded.loop();
        bounded_tracker.tick(bounded);
    }
    assert(bounded_scans == scans_at_expiry);

    // The primary four-bulb case: one controller key, four distinct identities
    // and four unique numbers, even if the advertiser address is reused/changes.
    test_preferences.clear();
    TestController four;
    esp32_ble_tracker::ESP32BLETracker four_tracker;
    std::array<FastconLight, 4> slots{FastconLight(0), FastconLight(0), FastconLight(0), FastconLight(0)};
    for (size_t i = 0; i < slots.size(); i++) {
        slots[i].set_slot(i + 1);
        slots[i].set_controller(&four);
        four.register_light(&slots[i]);
    }
    prepare(four, four_tracker);
    const auto shared_key = four.mesh_key_;
    four.discovery_.phase = DiscoveryPhase::RECOVERY;
    four.discovery_.candidate = 6000;
    four.discovery_io_ = TestController::DiscoveryIO::SCAN;
    for (uint8_t i = 0; i < 4; i++) {
        auto identity = factory;
        identity[4] += i;
        if (i % 2) identity.insert(identity.begin(), {0xf0, 0xff});
        receive(four, identity);
        receive(four, identity); // repeated identity never consumes another slot
    }
    assert(four.discovery_.phase == DiscoveryPhase::FACTORY); // boot window preempts brute force
    assert(four.discovery_.candidate == 6000 && four.mesh_key_ == shared_key);
    assert(four.discovery_.count == 4);
    unsigned keyed_ids = 0;
    for (int step = 0; step < 8; step++) {
        four.clear_queue();
        four.send_discovery_round_();
        assert(four.queue_.size() == 1);
        std::vector<uint8_t> rf(15, 0);
        const auto &packet = four.queue_.front().data;
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        for (size_t i = 21; i < 37; i++) rf[i] ^= DEFAULT_ENCRYPT_KEY[(i - 21) & 3];
        if (((rf[21] >> 4) & 7) != 2) continue;
        const uint8_t id = rf[31];
        assert(id >= 1 && id <= 4 && rf[32] == 1);
        assert(std::equal(rf.begin() + 25, rf.begin() + 31, four.discovery_.bulbs[id - 1].did.begin()));
        assert(std::equal(rf.begin() + 33, rf.begin() + 37, shared_key.begin()));
        keyed_ids |= 1 << (id - 1);
    }
    assert(keyed_ids == 0x0f); // per-bulb MAC + number, one shared new key
    // Each of four pending identities alternates independently; a global toggle
    // would starve some bulbs when round-robin cardinality is even.
    unsigned probed_ids = 0, retried_ids = 0;
    for (int step = 0; step < 16; step++) {
        four.clear_queue();
        four.send_discovery_round_();
        std::vector<uint8_t> rf(15, 0);
        const auto &packet = four.queue_.front().data;
        rf.insert(rf.end(), packet.begin(), packet.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        const auto type = ((rf[21] ^ DEFAULT_ENCRYPT_KEY[0]) >> 4) & 7;
        if (type == 2) {
            const auto id = rf[31] ^ DEFAULT_ENCRYPT_KEY[2];
            assert(id >= 1 && id <= 4);
            retried_ids |= 1 << (id - 1);
        } else if (type == 5) {
            assert((rf[25] ^ shared_key[0]) == 0x22);
            const auto id = rf[26] ^ shared_key[1];
            assert(id >= 1 && id <= 4);
            probed_ids |= 1 << (id - 1);
        }
    }
    assert(probed_ids == 0x0f && retried_ids == 0x0f);
    four.clear_queue();
    for (uint8_t i = 0; i < 4; i++) {
        assert(four.discovery_.bulbs[i].light_id == i + 1);
        receive(four, heartbeat(shared_key, i + 1), 100 + i);
    }
    four.finish_discovery_round_();
    assert(four.ready());
    for (uint8_t i = 0; i < 4; i++) assert(slots[i].get_light_id() == i + 1);

    // Drive four-bulb onboarding only through timed loop(), advertisements and
    // scanner callbacks. Simulated bulbs install a key only after real SET_KEY.
    test_preferences.clear();
    test_advertisements.clear();
    TestController timed;
    esp32_ble_tracker::ESP32BLETracker timed_tracker;
    std::array<FastconLight, 4> timed_slots{FastconLight(0), FastconLight(0), FastconLight(0), FastconLight(0)};
    for (uint8_t i = 0; i < 4; i++) {
        timed_slots[i].set_slot(i + 1);
        timed_slots[i].set_controller(&timed);
        timed.register_light(&timed_slots[i]);
    }
    FastconLight timed_group(0);
    timed_group.set_group_id(0);
    timed_group.set_controller(&timed);
    light::LightState timed_group_state;
    timed_group.setup_state(&timed_group_state);
    prepare(timed, timed_tracker);
    timed.set_discovery(&timed_tracker, 4, 30);
    timed_group.write_state(&timed_group_state);
    std::array<unsigned, 4> set_keys{};
    std::array<uint8_t, 4> assigned_ids{};
    bool wake_seen = false;
    size_t processed_ads = 0;
    const auto onboarding_started = test_now;
    for (int step = 0; step < 5000 && !timed.ready(); step++) {
        test_now += 10;
        timed.loop();
        timed_tracker.tick(timed);
        while (processed_ads < test_advertisements.size()) {
            const auto &ad = test_advertisements[processed_ads++];
            assert(ad.size() == 31 && ad[3] == 27);
            std::vector<uint8_t> rf(15, 0);
            rf.insert(rf.end(), ad.begin() + 7, ad.end());
            WhiteningContext ctx;
            whitening_init(0x25, ctx);
            whitening_encode(rf, ctx);
            for (size_t i = 21; i < 37; i++) rf[i] ^= DEFAULT_ENCRYPT_KEY[(i - 21) & 3];
            const uint8_t type = (rf[21] >> 4) & 7;
            if (type == 0) wake_seen = true;
            if (type != 2) continue;
            const uint8_t bulb = rf[25] - factory[4];
            assert(bulb < 4 && wake_seen && set_keys[bulb]++ == 0);
            assigned_ids[bulb] = rf[31];
            assert(assigned_ids[bulb] == bulb + 1 && rf[32] == 1);
            assert(std::equal(rf.begin() + 33, rf.begin() + 37, timed.mesh_key_.begin()));
        }
        if (timed_tracker.scanning) {
            for (uint8_t i = 0; i < 4; i++) {
                if (set_keys[i]) receive(timed, heartbeat(timed.mesh_key_, assigned_ids[i]), 100 + i);
                else if (wake_seen) {
                    auto identity = factory;
                    identity[4] += i;
                    receive(timed, identity, 200 + i);
                }
            }
        }
    }
    assert(timed.ready() && timed.discovery_.phase == DiscoveryPhase::DONE);
    for (uint8_t i = 0; i < 4; i++)
        assert(set_keys[i] == 1 && timed_slots[i].get_light_id() == i + 1);
    timed.clear_queue();
    timed_group_state.current_values.state = true;
    timed_group_state.remote_values = timed_group_state.current_values;
    timed_group.write_state(&timed_group_state);
    assert(timed.get_queue_size() == 1); // first real group control after timed discovery
    std::cout << "Simulated timed four-bulb onboarding: " << test_now - onboarding_started
              << " ms; four SET_KEY installs and four confirmed slots\n";

    // Exhaust the entire keyspace through the real loop and scan state machine.
    test_preferences.clear();
    TestController sweep;
    esp32_ble_tracker::ESP32BLETracker sweep_tracker;
    prepare(sweep, sweep_tracker);
    sweep.discovery_.phase = DiscoveryPhase::RECOVERY;
    sweep.set_repeat_count(5);
    test_advertisements.clear();
    const auto started = test_now;
    for (int step = 0; step < 1000000 && sweep.discovery_.phase != DiscoveryPhase::FAILED; step++) {
        test_now += 10;
        sweep.loop();
        sweep_tracker.tick(sweep);
        if (sweep.discovery_io_ == TestController::DiscoveryIO::STOPPING && !sweep_tracker.scanning)
            sweep.on_scan_end();
    }
    assert(sweep.discovery_.phase == DiscoveryPhase::FAILED && sweep.discovery_.candidate == 10000);
    assert(sweep_tracker.starts == 12500 && test_advertisements.size() == 12500);
    unsigned probes = 0, wakes = 0;
    for (const auto &advertisement : test_advertisements) {
        std::vector<uint8_t> rf(15, 0);
        rf.insert(rf.end(), advertisement.begin() + 7, advertisement.end());
        WhiteningContext ctx;
        whitening_init(0x25, ctx);
        whitening_encode(rf, ctx);
        const auto type = ((rf[21] ^ DEFAULT_ENCRYPT_KEY[0]) >> 4) & 7;
        probes += type == 5;
        wakes += type == 0;
    }
    assert(probes == 10000 && wakes == 2500);
    std::cout << "Simulated 10000-key sweep: " << test_now - started << " ms; 10000 group probes plus 2500 reclaim WAKE rounds\n";
    std::cout << "Discovery runtime tests passed\n";
}
