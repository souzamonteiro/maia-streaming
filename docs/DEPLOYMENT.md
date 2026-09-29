# Deployment

## Initial topology

Ubuntu 24.04 LTS, systemd, Nginx and local SSD storage.

```text
Nginx :443
  /media/  -> 127.0.0.1:8090
  /hls/    -> 127.0.0.1:8090
  /control -> private/restricted path

Maia Streaming
  HTTP/control: 127.0.0.1:8090
  WebRTC UDP: configured public range
  storage: /srv/maia/media
```

Keep control endpoints private or mutually authenticated. Expose only required WebRTC UDP ports and public playback endpoints.

## Filesystem

```text
/srv/maia/media/
  assets/
  recordings/
  staging/
  hls/
  thumbnails/
```

The service account owns only required paths. Staging and processing areas should have quotas.

## Reverse proxy

Nginx terminates TLS and forwards authenticated/public media requests. Disable proxy buffering only where measurement shows it is beneficial; VOD behavior should be benchmarked rather than assumed.

## Maia Edge

A Maia Edge node can host the service and media storage while the public VPS routes HTTPS/control traffic to it. WebRTC deployment requires deliberate UDP/TURN topology and should follow the proven Maia Meet deployment rather than being silently tunneled through an HTTP reverse proxy.
