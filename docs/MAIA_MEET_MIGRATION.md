# Maia Meet SFU Migration Plan

## Objective

Reuse the already tested Maia Meet C++ WebRTC/SFU implementation without destabilizing Maia Meet.

## Decision: extraction, not permanent whole-repository submodule

A permanent `maia-meet` submodule would reverse the desired dependency direction: generic infrastructure would depend on a conferencing application. Instead, Maia Streaming becomes the owner of reusable media infrastructure and Maia Meet consumes it.

A temporary submodule may be used only during migration/regression work.

## Stage 0 — freeze the known-good baseline

Tag the Maia Meet commit that passed multi-participant simultaneous audio/video tests. Record:

- compiler and build flags;
- OpenSSL/libsrtp/ICE dependencies and versions;
- CPU/RAM;
- participant count;
- audio/video tracks per participant;
- aggregate ingress/egress bitrate;
- test duration;
- packet loss/jitter/RTT where available;
- crashes, sanitizer findings and reconnect behavior.

This baseline is the acceptance criterion.

## Stage 1 — inventory the `sfu/` boundary

Classify files into:

- generic networking/core;
- ICE/STUN;
- DTLS;
- SRTP;
- RTP/RTCP;
- packet cache;
- routing;
- conference/participant model;
- Maia Meet-specific control protocol;
- executable/bootstrap code.

Do not rename or redesign classes during this stage.

## Stage 2 — build reusable targets

Target shape:

```text
maia_core
maia_rtp
maia_webrtc
maia_sfu
maia_streaming_server
```

Keep an adapter for the existing Maia Meet signaling protocol.

## Stage 3 — dual build

Build the same SFU source from Maia Streaming and Maia Meet. Run existing Meet tests against both binaries. No functional additions until parity is demonstrated.

## Stage 4 — switch Maia Meet

Change Maia Meet deployment/build to consume Maia Streaming's SFU service/library. Preserve signaling messages or provide a compatibility adapter.

## Stage 5 — remove duplication

Only after production-like regression passes should duplicated SFU sources be removed from Maia Meet.

## Acceptance gates

Migration fails if it introduces material regression in:

- audio continuity;
- video continuity;
- join/leave/rejoin;
- concurrent streams;
- CPU per forwarded Mbps;
- resident memory per participant;
- packet loss under equivalent network conditions;
- latency;
- stability over the same soak-test duration.
