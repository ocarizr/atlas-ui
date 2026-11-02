# Window

`atlas::window` owns an OS client area without exposing SDK types. Create, pump,
resize and destroy it on one thread. Titles are UTF-8; client dimensions are
physical pixels. Initial dimensions must be between 1 and 16384. Message snapshots
report minimized/closed state and DPI relative to 96 DPI.

`poll_events()` dispatches pending messages. `wait_for_events()` provides a bounded
coordinator wait when minimized or occluded. `request_close()` hides the window
and marks it closed; it does not invalidate a handle retained by a presentation
surface. Closing one client does not post a process-wide quit message.

The application declares DPI awareness (game/editor use PerMonitorV2). The library
does not silently change process-global DPI policy. Native code is selected through
`ATLAS_TARGET_PLATFORM=windows` and `ATLAS_BUILD_WINDOWS_INTEGRATION=ON`. With SDK
integration disabled, configuration checks still run and creation explicitly
throws because no window backend is present. Other target platforms remain future
adapters, not supported implementations.
