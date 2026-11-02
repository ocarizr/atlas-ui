# Atlas renderer content

`atlas::renderer_content` orchestrates streamer buffers, ready-job image/shader
preparation and coordinator-owned GPU content. It links renderer and streaming;
the renderer itself still has no storage, codec or job dependency.

See [content integration](../../docs/33_renderer_content.md) for ownership,
cancellation, bounded image decoding, exact-key pipeline caching and application
usage. Windows uses WIC on memory buffers; SDK-free callers can inject a decoder.
