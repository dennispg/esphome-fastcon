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
        if (controller.discovery_io_ == TestController::DiscoveryIO::SCAN)
        {
            scanned = true;
            if (!reply.empty()) receive(controller, reply);
        }
        if (controller.discovery_io_ == TestController::DiscoveryIO::STOPPING && !tracker.scanning)
            controller.on_scan_end();
        if (scanned && controller.discovery_io_ == TestController::DiscoveryIO::REST)
            return;
    }
    assert(false && "nonblocking scan round did not finish");
}

static void prepare(TestController &controller, esp32_ble_tracker::ESP32BLETracker &tracker, uint8_t max = 4)
{
    controller.set_preference_key(1);
    controller.set_discovery(&tracker, max, 1);
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
    const auto before_control = test_advertisements.size();
    for (int step = 0; step < 20; step++) {
        test_now += 10;
        controller.loop();
    }
    assert(test_advertisements.size() >= before_control);
    assert(test_advertisements.back()[3] + 4u == test_advertisements.back().size());
    controller.queueCommand(2, controller.single_control(2, {0xff}));
    for (int step = 0; step < 20; step++) {
        test_now += 10;
        controller.loop();
    }
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
    prepare(pending, pending_tracker, 4);
    assert(pending.mesh_key_ == key && pending.discovery_.count == 1);

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
    four.clear_queue();
    for (uint8_t i = 0; i < 4; i++) {
        assert(four.discovery_.bulbs[i].light_id == i + 1);
        receive(four, heartbeat(shared_key, i + 1), 100 + i);
    }
    four.finish_discovery_round_();
    assert(four.ready());
    for (uint8_t i = 0; i < 4; i++) assert(slots[i].get_light_id() == i + 1);

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
