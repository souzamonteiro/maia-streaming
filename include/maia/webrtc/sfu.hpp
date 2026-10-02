#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <cstdint>
#include <optional>
#include <functional>
#include "maia/core/json.hpp"
#include "maia/media/recording.hpp"

namespace maia::webrtc {

struct RtpPacket {
    std::uint8_t payload_type = 0;
    std::uint16_t sequence_number = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t ssrc = 0;
    bool marker = false;
    std::vector<std::uint8_t> raw_data;

    static std::optional<RtpPacket> parse(const std::uint8_t* data, std::size_t len);
};

class PacketCache {
public:
    explicit PacketCache(std::size_t capacity = 512);
    void store(const RtpPacket& packet);
    [[nodiscard]] std::optional<RtpPacket> find(std::uint16_t sequence_number) const;

private:
    std::size_t capacity_;
    std::deque<RtpPacket> packets_;
    mutable std::mutex mutex_;
};

struct Track {
    std::string id;
    std::string kind = "video"; // "video" or "audio"
    std::string codec = "vp8";  // "vp8" or "opus"
    std::uint32_t ssrc = 0;
    std::string participant_id;
    bool muted = false;

    [[nodiscard]] core::Json to_json() const;
    static Track from_json(const core::Json& j);
};

struct Participant {
    std::string id;
    std::string name;
    std::string joined_at;
    std::map<std::string, Track> published_tracks;
    std::vector<std::string> subscribed_track_ids;

    [[nodiscard]] core::Json to_json() const;
};

class Room {
public:
    Room(std::string id, std::string name);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    bool add_participant(const Participant& p);
    bool remove_participant(std::string_view pid);
    [[nodiscard]] std::optional<Participant> get_participant(std::string_view pid) const;
    [[nodiscard]] std::vector<Participant> list_participants() const;

    bool publish_track(std::string_view pid, const Track& track);
    bool subscribe_track(std::string_view pid, std::string_view track_id);
    void route_packet(const RtpPacket& packet);

    void attach_recording(std::shared_ptr<media::RecordingSession> rec);
    void detach_recording();

    [[nodiscard]] std::size_t participant_count() const;
    [[nodiscard]] std::size_t forwarded_packet_count() const noexcept { return packets_forwarded_; }

    [[nodiscard]] core::Json to_json() const;

private:
    std::string id_;
    std::string name_;
    mutable std::mutex mutex_;
    std::map<std::string, Participant> participants_;
    std::map<std::uint32_t, std::shared_ptr<PacketCache>> track_caches_;
    std::shared_ptr<media::RecordingSession> recording_;
    std::uint64_t packets_forwarded_{0};
};

class SfuServer {
public:
    SfuServer();

    std::shared_ptr<Room> create_room(std::string_view room_id, std::string_view name = "");
    std::shared_ptr<Room> get_room(std::string_view room_id) const;
    std::vector<std::shared_ptr<Room>> list_rooms() const;
    bool remove_room(std::string_view room_id);

    // Signaling protocol handler
    core::Json handle_signal(std::string_view room_id, const core::Json& message);

    [[nodiscard]] std::size_t active_rooms_count() const;
    [[nodiscard]] std::size_t active_participants_count() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Room>> rooms_;
};

} // namespace maia::webrtc

