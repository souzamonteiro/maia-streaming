# Control and Delivery API

All `/v1` control endpoints are versioned. Application authentication and end-user playback capabilities are distinct.

## Health

`GET /healthz` — process liveness.

`GET /readyz` — readiness: configuration loaded, storage accessible and required workers initialized.

## Assets

`POST /v1/assets` — register an upload or media object.

`GET /v1/assets/{assetId}` — metadata.

`DELETE /v1/assets/{assetId}` — mark/delete according to retention policy.

## Playback capability

`POST /v1/assets/{assetId}/playback-token` — trusted application endpoint. Returns a short-lived token/capability.

## VOD delivery

`GET|HEAD /v1/media/{assetId}/content`

Headers supported initially:

- `Authorization: Bearer ...`
- `Range`
- `If-None-Match`
- `If-Range`

Responses include `Accept-Ranges`, `ETag`, `Content-Type`, `Content-Length`, and `Content-Range` when applicable.

## HLS

`GET /v1/hls/{assetId}/master.m3u8`

`GET /v1/hls/{assetId}/{rendition}/index.m3u8`

`GET /v1/hls/{assetId}/{rendition}/{segment}`

## Processing

`POST /v1/assets/{assetId}/jobs`

Example job types: `normalize`, `thumbnail`, `extract-audio`, `hls-package`, `transcode`.

`GET /v1/jobs/{jobId}`

## Recording

Introduced in v0.4:

`POST /v1/recordings`

`POST /v1/recordings/{recordingId}/stop`

`GET /v1/recordings/{recordingId}`

## API rules

- Never expose filesystem paths.
- IDs are opaque.
- Mutating control calls are authenticated service-to-service.
- Playback tokens are short-lived and least-privilege.
- Range requests do not trigger transcoding.
- Error responses use a stable machine-readable code plus human-readable message.
