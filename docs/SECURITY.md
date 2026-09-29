# Security

## Principles

- Treat uploaded media and network packets as untrusted.
- Keep parsers bounded and reject malformed input early.
- Never implement cryptographic primitives when mature libraries exist.
- Run as an unprivileged service account.
- Keep storage outside the public web root.
- Use opaque asset identifiers, never client-provided filesystem paths.
- Separate trusted control credentials from end-user playback tokens.
- Prefer short token lifetimes and support signing-key rotation.
- Rate-limit control endpoints and abusive playback/session creation.
- Bound all queues, caches and per-client buffers.

## WebRTC

Retain the Maia Meet security boundary: DTLS establishes keys; SRTP/SRTCP protects media transport. The SFU necessarily terminates transport encryption to inspect/route RTP headers.

## Upload processing

Run FFmpeg/media probing with resource limits and, for production hardening, an isolated worker account/container. Enforce maximum input size, duration and processing time.

## HTTP

TLS terminates at Nginx initially. Validate Range arithmetic carefully to prevent integer overflow and out-of-bounds reads. Do not reflect arbitrary storage metadata into response headers.
