# Atlas render host

`atlas::render_host` provides composition-owned persistent render coordination.
The main thread owns window messages and admits increasing frame identities through
a bounded, nonblocking FIFO. The render thread constructs, calls, drains and
destroys the injected handler. The library creates no job pool and does not link
renderer, streaming, application or editor implementations.

Use `window::share_presentation()` for a synchronized native presentation endpoint.
Keep the native window alive and pumping until `stop_and_join(pump)` returns,
including exception paths. Optional immutable `frame_payload` objects carry CPU
packets through the queue; their destructors must be safe on either handoff thread.
See [renderer readiness](../../docs/34_renderer_readiness.md) for the complete
ownership, failure, backpressure and diagnostics contracts.
