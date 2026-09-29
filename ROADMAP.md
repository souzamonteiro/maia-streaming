# Maia Streaming Roadmap

## v0.1 — Native VOD foundation

- C++20 service skeleton and configuration.
- HTTP GET/HEAD.
- Video and audio byte-range requests.
- 200/206/304/404/416 behavior.
- Local filesystem storage adapter.
- Asset metadata model.
- Short-lived playback token verification.
- Health/readiness endpoints.
- Structured logs and initial Prometheus metrics.
- HTML5 video/audio example player.
- Unit/integration tests for range semantics.

**Exit criterion:** Chrome/Firefox can seek repeatedly through large video/audio assets while concurrent clients receive correct byte ranges without unbounded memory growth.

## v0.2 — Maia Meet real-time integration

- Freeze known-good Maia Meet SFU baseline.
- Extract reusable C++ targets without behavior changes.
- Preserve Opus/VP8 behavior.
- Preserve ICE/DTLS/SRTP/RTP/RTCP pipeline.
- Maia Meet compatibility adapter.
- Multi-participant simultaneous audio/video regression suite.
- Soak and load benchmarks.

**Exit criterion:** existing Maia Meet scenarios pass with no material stability/performance regression.

## v0.3 — HLS VOD

- HLS master/media playlists.
- Segment storage and protected delivery.
- Background FFmpeg packaging.
- Multiple rendition metadata.
- Player example with native HLS where supported and a documented JS fallback strategy.
- Cache policy and immutable segment semantics.

## v0.4 — Recording pipeline

- Server-side recording architecture.
- Recording lifecycle API.
- Finalization jobs.
- Generate normalized VOD asset after a Maia Meet session.
- Optional separate audio artifact.
- Thumbnail/poster generation.
- Automatic handoff hook for Maia LMS.

## v0.5 — Production media platform

- Object/S3-compatible storage adapter.
- Media node directory and room affinity.
- Quotas and bandwidth controls.
- Signed URLs/capabilities with key rotation.
- Processing worker isolation.
- Extended metrics/dashboards.
- Backup/retention hooks.

## v1.0

- Stable control API.
- Stable asset schema.
- Stable Maia Meet and Maia LMS integrations.
- Upgrade/migration policy.
- Security review.
- Load/soak test targets documented and reproducible.
