# Renderer jobs adapter

Link `atlas::renderer_jobs` to record coarse command batches through an existing
`atlas::jobs::job_system`. The application owns the pool and passes
`renderer_jobs::executor{pool.dispatcher()}` to `pass_renderer::submit` with a
measured `recording_options` policy. The adapter preserves dispatcher options,
uses capture-free joined ranges and creates no threads or graphics services.

All accepted recording work joins before return or exception. Worker bodies never
wait for storage, presentation or GPU fences. Resource validation, queue submission
and lifetime publication remain on the renderer coordinator. The pool must outlive
the adapter's use. See [parallel recording](../../docs/renderer_recording.md).
