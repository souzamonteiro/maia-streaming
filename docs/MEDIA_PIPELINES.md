# Media Pipelines

## Uploaded course video

```text
upload -> quarantine/staging -> validate -> checksum -> normalize -> metadata -> publish
                                                        |-> thumbnails
                                                        |-> optional HLS renditions
```

Normalization should produce browser-friendly media and move MP4 metadata for progressive playback when appropriate.

## Uploaded audio

```text
upload -> validate -> metadata -> optional normalization -> publish
```

## VOD playback

```text
browser -> token validation -> asset lookup -> range validation -> storage -> socket
```

No decode/transcode occurs on this path.

## HLS playback

```text
browser -> authorized manifest -> selected rendition -> immutable segments
```

## Maia Meet live session

```text
browser publishers -> WebRTC/SRTP -> SFU -> WebRTC/SRTP -> subscribers
```

The SFU forwards encoded audio/video and handles transport/routing metadata.

## Meet-to-LMS recording (target)

```text
Maia Meet -> recording sink -> raw/final recording -> processing job
          -> normalized asset -> HLS/thumbnails -> publish event -> Maia LMS
```

Recording and processing must not block the SFU network loop.
