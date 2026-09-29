# Testing Strategy

## Unit tests

- byte-range parser: valid, suffix, open-ended, malformed, overflow;
- asset ID validation;
- MIME mapping;
- token expiry/signature/claims;
- ETag generation;
- job state machine;
- bounded queue behavior.

## VOD integration tests

- GET full object;
- HEAD;
- first/middle/final byte range;
- seek patterns from browser;
- invalid and unsatisfiable ranges;
- disconnect during transfer;
- concurrent video and audio clients;
- slow readers/backpressure;
- file replacement protection via immutable IDs/checksums.

## WebRTC regression

The Maia Meet known-good scenario is mandatory:

- multiple participants;
- simultaneous audio and video;
- join/leave/rejoin;
- mute/unmute and camera state changes;
- sustained operation/soak;
- network impairment where practical;
- compare CPU, RAM, bandwidth, packet loss, jitter and RTT with baseline.

## Tooling

Use sanitizers in CI/debug builds where compatible: AddressSanitizer and UndefinedBehaviorSanitizer first; ThreadSanitizer in targeted concurrency jobs.

Fuzz parsers that accept hostile binary/text input, especially RTP/RTCP and HTTP Range parsing.
