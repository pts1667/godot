# Module Archetypes

Use the closest existing module as the starting template.

- `modules/zip/`: small built-in module with docs, tests, simple `SCsub`, and standard `register_types.*`.
- `modules/jsonrpc/`: minimal module shape with conditional test source inclusion.
- `modules/webrtc/`: scene-level class registration in `register_types.cpp`.
- `modules/websocket/`: module with editor-only sources, platform branches, thirdparty sources, and explicit object dependency wiring.
- `modules/webxr/`: platform-specific JavaScript asset wiring in `SCsub`.
- `modules/openxr/`: complex thirdparty-backed module with platform defines and preregister behavior.
- `modules/text_server_fb/`: default-off module with `is_enabled()` and module dependencies in `config.py`.
- `modules/mp3/`: simple `get_opts(platform)` example for SCons options.