#include <cassert>
#include <algorithm>
#include "protocol.h"

using namespace esphome::fastcon;

int main()
{
    const std::array<uint8_t, 4> key{0x31, 0x32, 0x33, 0x34};
    assert(app_mesh_key(0) == (std::array<uint8_t, 4>{0x30, 0x30, 0x30, 0x30}));
    assert(app_mesh_key(9999) == (std::array<uint8_t, 4>{0x39, 0x39, 0x39, 0x39}));
    assert(app_mesh_key(1234) == key);
    std::array<uint8_t, 6> did{};
    // Captured factory identity layout, company ID stripped by ESPHome.
    std::vector<uint8_t> factory{0x4e, 1, 2, 3, 0xe8, 0x70, 0x72, 0xf2, 0xe7, 0x70,
                                 0x2a, 0xa8, 0x5e, 0x36, 0x7b, 0xc4};
    assert(decode_factory_identity(factory, did));
    assert(did[0] == 0xe8 && did[5] == 0x70);
    auto full_factory = factory;
    full_factory.insert(full_factory.begin(), {0xf0, 0xff});
    assert(full_factory.size() == 18 && decode_factory_identity(full_factory, did));
    assert(did[0] == 0xe8 && did[5] == 0x70);
    full_factory[17] ^= 1;
    assert(!decode_factory_identity(full_factory, did));
    factory[12] ^= 1;
    assert(!decode_factory_identity(factory, did));
    factory.resize(15);
    assert(!decode_factory_identity(factory, did));

    // Old-protocol heartbeat: factory-XOR header, mesh-XOR body. Not RF wrapped.
    std::vector<uint8_t> heartbeat{0x30, 7, 0x34, 0, 0x34, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t sum = 0;
    for (size_t i = 0; i < heartbeat.size(); i++)
        if (i != 3) sum += heartbeat[i];
    heartbeat[3] = sum;
    for (size_t i = 0; i < heartbeat.size(); i++)
        heartbeat[i] ^= i < 4 ? DEFAULT_ENCRYPT_KEY[i] : key[(i - 4) & 3];
    uint8_t id = 0;
    assert(decode_heartbeat(heartbeat, key, id) && id == 2);
    auto full_heartbeat = heartbeat;
    full_heartbeat.insert(full_heartbeat.begin(), {0xf0, 0xff});
    assert(decode_heartbeat(full_heartbeat, key, id) && id == 2);
    assert(!decode_heartbeat(heartbeat, app_mesh_key(1235), id));
    heartbeat[3] ^= 1;
    assert(!decode_heartbeat(heartbeat, key, id));
    heartbeat[3] ^= 1;
    heartbeat.resize(15);
    assert(!decode_heartbeat(heartbeat, key, id));

    // Captured flood-light layout: byte 2 contains the light number, not key[3].
    std::vector<uint8_t> flood_heartbeat{0x30, 0x80, 0x02, 0x7e, 0x74, 0x02, 0x00, 0x04,
                                         0x04, 0x35, 0x0a, 0x0f, 0, 0, 0, 0};
    for (size_t i = 0; i < flood_heartbeat.size(); i++)
        flood_heartbeat[i] ^= i < 4 ? DEFAULT_ENCRYPT_KEY[i] : key[(i - 4) & 3];
    assert(decode_heartbeat(flood_heartbeat, key, id) && id == 2);
    assert(!decode_heartbeat(flood_heartbeat, app_mesh_key(1235), id));

    // Framing must reuse the existing RF/CRC/whitening codec.
    std::vector<uint8_t> body{0x20, 1, 0xc4, 0};
    std::vector<uint8_t> data{0xe8, 0x70, 0x72, 0xf2, 0xe7, 0x70, 1, 1, 0x31, 0x32, 0x33, 0x34};
    body.insert(body.end(), data.begin(), data.end());
    sum = 0;
    for (auto b : body) sum += b;
    body[3] = sum;
    for (size_t i = 0; i < body.size(); i++) body[i] ^= DEFAULT_ENCRYPT_KEY[i & 3];
    assert(encode_discovery_command(2, 1, data, DEFAULT_ENCRYPT_KEY, 0xc4) ==
           prepare_payload({0xc1, 0xc2, 0xc3}, body));
    assert(encode_discovery_command(0, 1, {}, DEFAULT_ENCRYPT_KEY, 0xff).size() == 24);
    // Byte-for-byte vectors from AdysTech/br-mesh-controller's reference codec.
    assert(vector_to_hex_string(encode_discovery_command(0, 2, {}, DEFAULT_ENCRYPT_KEY, 0xff)) ==
           "6DB64368931D0D075C7FC63E5F0F65CA0A67AA63130BAE81");
    assert(vector_to_hex_string(encode_discovery_command(2, 3, data, DEFAULT_ENCRYPT_KEY, 0xc4)) ==
           "6DB64368931D2D0667B82E4E2DFD82BA0B669B51203FFB72");
}
