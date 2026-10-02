#include "maia/webrtc/sfu.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>

int main() {
    using maia::webrtc::RtpPacket;
    using maia::webrtc::PacketCache;
    using maia::webrtc::SfuServer;
    using maia::webrtc::Participant;
    using maia::webrtc::Track;

    // 1. RTP Packet Serialization and Parsing
    std::vector<std::uint8_t> rtp_buf(12 + 10);
    rtp_buf[0] = 0x80; // V=2, P=0, X=0, CC=0
    rtp_buf[1] = 0xE0; // M=1, PT=96 (H264/VP8)
    uint16_t seq = htons(12345);
    std::memcpy(&rtp_buf[2], &seq, 2);
    uint32_t ts = htonl(987654321);
    std::memcpy(&rtp_buf[4], &ts, 4);
    uint32_t ssrc = htonl(0xAABBCCDD);
    std::memcpy(&rtp_buf[8], &ssrc, 4);
    std::memcpy(&rtp_buf[12], "HELLO_SFU!", 10);

    auto parsed_opt = RtpPacket::parse(rtp_buf.data(), rtp_buf.size());
    assert(parsed_opt.has_value());
    assert(parsed_opt->marker == true);
    assert(parsed_opt->payload_type == 96);
    assert(parsed_opt->sequence_number == 12345);
    assert(parsed_opt->timestamp == 987654321);
    assert(parsed_opt->ssrc == 0xAABBCCDD);
    assert(parsed_opt->raw_data == rtp_buf);

    // Malformed packet
    std::uint8_t malformed[10] = {0};
    assert(!RtpPacket::parse(malformed, sizeof(malformed)).has_value());

    // 2. Packet Cache
    PacketCache cache(4);
    cache.store(*parsed_opt);
    auto found = cache.find(12345);
    assert(found.has_value());
    assert(found->ssrc == 0xAABBCCDD);
    assert(!cache.find(9999).has_value());

    // 3. SfuServer Room and Participant Management
    SfuServer server;
    assert(server.active_rooms_count() == 0);

    auto room = server.create_room("room-test-01", "Town Hall");
    assert(room != nullptr);
    assert(room->id() == "room-test-01");
    assert(room->name() == "Town Hall");
    assert(server.active_rooms_count() == 1);
    assert(server.active_participants_count() == 0);

    Participant alice;
    alice.id = "user-alice";
    alice.name = "Alice";
    assert(room->add_participant(alice));

    Participant bob;
    bob.id = "user-bob";
    bob.name = "Bob";
    assert(room->add_participant(bob));

    assert(server.active_participants_count() == 2);
    assert(room->participant_count() == 2);

    // Track publication
    Track video_track;
    video_track.id = "trk-video-01";
    video_track.kind = "video";
    video_track.codec = "vp8";
    video_track.ssrc = 0xAABBCCDD;
    video_track.participant_id = "user-alice";
    assert(room->publish_track("user-alice", video_track));

    // Route packet through room
    room->route_packet(*parsed_opt);
    assert(room->forwarded_packet_count() == 1);

    // 4. Signaling protocol handling
    maia::core::Json join_sig = maia::core::Json::object();
    join_sig["type"] = "join";
    join_sig["participant_id"] = "user-charlie";
    join_sig["name"] = "Charlie";

    auto resp = server.handle_signal("room-test-01", join_sig);
    assert(resp.is_object());
    assert(resp["type"].as_string() == "joined");
    assert(server.active_participants_count() == 3);

    // Leave
    maia::core::Json leave_sig = maia::core::Json::object();
    leave_sig["type"] = "leave";
    leave_sig["participant_id"] = "user-charlie";
    auto resp_leave = server.handle_signal("room-test-01", leave_sig);
    assert(resp_leave["type"].as_string() == "left");
    assert(server.active_participants_count() == 2);

    assert(room->remove_participant("user-alice"));
    assert(room->remove_participant("user-bob"));
    assert(server.active_participants_count() == 0);

    assert(server.remove_room("room-test-01"));
    assert(server.active_rooms_count() == 0);

    std::cout << "[Test PASS] SfuServer, RTP parsing, packet cache and signaling tests\n";
    return 0;
}
