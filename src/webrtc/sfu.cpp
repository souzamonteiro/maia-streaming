#include "maia/webrtc/sfu.hpp"
#include "maia/core/utils.hpp"
#include <arpa/inet.h>
#include <iostream>
#include <cstring>

namespace maia::webrtc {

std::optional<RtpPacket> RtpPacket::parse(const std::uint8_t* data, std::size_t len) {
    if (len < 12) return std::nullopt;

    uint8_t first_byte = data[0];
    uint8_t version = (first_byte >> 6) & 0x03;
    if (version != 2) return std::nullopt;

    uint8_t cc = first_byte & 0x0F;
    std::size_t min_len = 12 + (cc * 4);
    if (len < min_len) return std::nullopt;

    RtpPacket pkt;
    pkt.marker = (data[1] & 0x80) != 0;
    pkt.payload_type = data[1] & 0x7F;

    uint16_t seq = 0;
    std::memcpy(&seq, data + 2, 2);
    pkt.sequence_number = ntohs(seq);

    uint32_t ts = 0;
    std::memcpy(&ts, data + 4, 4);
    pkt.timestamp = ntohl(ts);

    uint32_t ssrc = 0;
    std::memcpy(&ssrc, data + 8, 4);
    pkt.ssrc = ntohl(ssrc);

    pkt.raw_data.assign(data, data + len);
    return pkt;
}

PacketCache::PacketCache(std::size_t capacity) : capacity_(capacity) {}

void PacketCache::store(const RtpPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (packets_.size() >= capacity_) {
        packets_.pop_front();
    }
    packets_.push_back(packet);
}

std::optional<RtpPacket> PacketCache::find(std::uint16_t sequence_number) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pkt : packets_) {
        if (pkt.sequence_number == sequence_number) {
            return pkt;
        }
    }
    return std::nullopt;
}

core::Json Track::to_json() const {
    core::Json j = core::Json::object();
    j["id"] = id;
    j["kind"] = kind;
    j["codec"] = codec;
    j["ssrc"] = ssrc;
    j["participant_id"] = participant_id;
    j["muted"] = muted;
    return j;
}

Track Track::from_json(const core::Json& j) {
    Track t;
    t.id = j.get("id").as_string();
    t.kind = j.get("kind").as_string("video");
    t.codec = j.get("codec").as_string("vp8");
    t.ssrc = static_cast<std::uint32_t>(j.get("ssrc").as_uint64());
    t.participant_id = j.get("participant_id").as_string();
    t.muted = j.get("muted").as_bool();
    return t;
}

core::Json Participant::to_json() const {
    core::Json j = core::Json::object();
    j["id"] = id;
    j["name"] = name;
    j["joined_at"] = joined_at;

    core::Json tracks_arr = core::Json::array();
    for (const auto& [tid, t] : published_tracks) {
        tracks_arr.push_back(t.to_json());
    }
    j["tracks"] = std::move(tracks_arr);
    return j;
}

Room::Room(std::string id, std::string name)
    : id_(std::move(id)), name_(std::move(name)) {
    if (name_.empty()) name_ = id_;
}

bool Room::add_participant(const Participant& p) {
    std::lock_guard<std::mutex> lock(mutex_);
    participants_[p.id] = p;
    return true;
}

bool Room::remove_participant(std::string_view pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    return participants_.erase(std::string(pid)) > 0;
}

std::optional<Participant> Room::get_participant(std::string_view pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(std::string(pid));
    if (it != participants_.end()) return it->second;
    return std::nullopt;
}

std::vector<Participant> Room::list_participants() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Participant> list;
    list.reserve(participants_.size());
    for (const auto& [id, p] : participants_) {
        list.push_back(p);
    }
    return list;
}

bool Room::publish_track(std::string_view pid, const Track& track) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(std::string(pid));
    if (it == participants_.end()) return false;

    it->second.published_tracks[track.id] = track;
    if (track.ssrc != 0 && track_caches_.find(track.ssrc) == track_caches_.end()) {
        track_caches_[track.ssrc] = std::make_shared<PacketCache>();
    }
    return true;
}

bool Room::subscribe_track(std::string_view pid, std::string_view track_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(std::string(pid));
    if (it == participants_.end()) return false;

    it->second.subscribed_track_ids.push_back(std::string(track_id));
    return true;
}

void Room::route_packet(const RtpPacket& packet) {
    std::shared_ptr<media::RecordingSession> rec_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        packets_forwarded_++;

        auto cache_it = track_caches_.find(packet.ssrc);
        if (cache_it != track_caches_.end()) {
            cache_it->second->store(packet);
        }

        rec_copy = recording_;
    }

    // Tap into recording if active
    if (rec_copy) {
        rec_copy->write_packet(packet.raw_data.data(), packet.raw_data.size());
    }
}

void Room::attach_recording(std::shared_ptr<media::RecordingSession> rec) {
    std::lock_guard<std::mutex> lock(mutex_);
    recording_ = std::move(rec);
}

void Room::detach_recording() {
    std::lock_guard<std::mutex> lock(mutex_);
    recording_.reset();
}

std::size_t Room::participant_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return participants_.size();
}

core::Json Room::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    core::Json j = core::Json::object();
    j["id"] = id_;
    j["name"] = name_;
    j["participant_count"] = participants_.size();
    j["packets_forwarded"] = packets_forwarded_;
    j["recording_active"] = (recording_ != nullptr);

    core::Json parts = core::Json::array();
    for (const auto& [id, p] : participants_) {
        parts.push_back(p.to_json());
    }
    j["participants"] = std::move(parts);
    return j;
}

SfuServer::SfuServer() {}

std::shared_ptr<Room> SfuServer::create_room(std::string_view room_id, std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = room_id.empty() ? core::generate_id("room") : std::string(room_id);
    auto room = std::make_shared<Room>(id, std::string(name));
    rooms_[id] = room;
    return room;
}

std::shared_ptr<Room> SfuServer::get_room(std::string_view room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(std::string(room_id));
    if (it != rooms_.end()) return it->second;
    return nullptr;
}

std::vector<std::shared_ptr<Room>> SfuServer::list_rooms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Room>> list;
    list.reserve(rooms_.size());
    for (const auto& [id, r] : rooms_) {
        list.push_back(r);
    }
    return list;
}

bool SfuServer::remove_room(std::string_view room_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.erase(std::string(room_id)) > 0;
}

core::Json SfuServer::handle_signal(std::string_view room_id, const core::Json& msg) {
    std::string type = msg.get("type").as_string();
    std::string pid = msg.get("participant_id").as_string();

    auto room = get_room(room_id);
    if (!room) {
        room = create_room(room_id);
    }

    core::Json resp = core::Json::object();
    resp["room_id"] = room->id();
    resp["timestamp"] = core::iso8601_now();

    if (type == "join") {
        Participant p;
        p.id = pid.empty() ? core::generate_id("peer") : pid;
        p.name = msg.get("name").as_string("Guest");
        p.joined_at = core::iso8601_now();
        room->add_participant(p);

        resp["type"] = "joined";
        resp["participant_id"] = p.id;
        resp["room"] = room->to_json();
    } else if (type == "leave") {
        room->remove_participant(pid);
        resp["type"] = "left";
        resp["participant_id"] = pid;
    } else if (type == "publish") {
        Track track;
        track.id = msg.get("track_id").as_string(core::generate_id("trk"));
        track.kind = msg.get("kind").as_string("video");
        track.codec = msg.get("codec").as_string("vp8");
        track.ssrc = static_cast<std::uint32_t>(msg.get("ssrc").as_uint64());
        track.participant_id = pid;
        room->publish_track(pid, track);

        resp["type"] = "published";
        resp["track"] = track.to_json();
    } else if (type == "subscribe") {
        std::string tid = msg.get("track_id").as_string();
        room->subscribe_track(pid, tid);
        resp["type"] = "subscribed";
        resp["track_id"] = tid;
    } else if (type == "offer" || type == "answer") {
        // Forward SDP descriptors or acknowledge negotiation
        resp["type"] = (type == "offer") ? "answer" : "ack";
        resp["sdp"] = msg.get("sdp").as_string();
    } else {
        resp["type"] = "ack";
        resp["status"] = "ok";
    }

    return resp;
}

std::size_t SfuServer::active_rooms_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}

std::size_t SfuServer::active_participants_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& [id, r] : rooms_) {
        total += r->participant_count();
    }
    return total;
}

} // namespace maia::webrtc

