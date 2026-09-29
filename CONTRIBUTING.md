# Contributing

- C++20, warnings enabled in CI.
- Keep the WebRTC forwarding hot path free from blocking disk I/O and transcoding.
- Prefer bounded containers/queues for network-controlled data.
- Add tests for protocol parsing and state transitions.
- Preserve Maia Meet SFU behavior during extraction; refactoring and feature work should be separate changes.
- Document externally visible protocol/API changes.
