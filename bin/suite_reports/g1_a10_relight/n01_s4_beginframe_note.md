# S4 N03 BeginFrame-before-stream

`WorldViewBinding::TickWorldStreamingPhase` now calls `UFrameDeadline::BeginFrame`
**before** `UpdateStreaming` / `TickAsyncChunkSystems` (was after stream).

Diet (ex-A5 stream census reuse) remains **deferred** — A4 evidence still shows
`prep_schedule_policy_ms` ~0; dominant residual `streamer_update_ms` /
`async_io_ms`. Do not land census SoT diet without dual-lane stop-line + eye.
