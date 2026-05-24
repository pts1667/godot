The main gaps are these:

1. External animation blocks and sectioned animation data. Done for the vendored sampler, SourcePPMDL, and SourceAnimPlayer. Disk-based loads now resolve companion .ani blobs automatically, and buffer-based loads can accept animblock data explicitly.

2. Full Source sequence dependency semantics. Largely done for the current runtime path. SourceAnimPlayer now evaluates autoplay sequences, captures autoplay locks before the autoplay pass, solves them after the pass, and inherits sampled local hierarchy overrides in addition to sequence autolayers and per-sequence iklocks. The main remaining gaps in this area are the more exotic Source control systems beyond this core dependency path.

3. Proper skinned import, not just static mesh baking. The public wrapper surface in sourcepp_mdl.h has mesh and animation APIs, but no skeleton/skin creation APIs. More importantly, sourcepp_mdl.cpp builds surfaces from positions, normals, UVs, and indices only. It does not write bone indices, bone weights, tangents, or a Godot skeleton/skin resource. So “full MDL support” still needs a real skeletal import path.

4. Bone-controller and related animation control systems. The parser already stores bone controllers in MDL.h, but there is no corresponding Godot API or runtime evaluation surface in sourcepp_mdl.h or source_anim_player.h. If you want parity with Source animation behavior, that control layer still needs to be surfaced and applied.

5. Local hierarchy and other higher-order animation data. Local hierarchy overrides are now applied during animation sampling, so they affect both SourcePPMDL animation extraction and SourceAnimPlayer runtime playback. The remaining gap is that the raw hierarchy records are still not surfaced as a richer parsed public representation, and other higher-order control data is still missing.

6. Facial systems and other unparsed MDL subsystems. The vendored MDL struct still has flexes, flex controllers, flex rules, eyeballs, mouths, include-models, and flex UI fields commented out in MDL.h, MDL.h, MDL.h, and MDL.h. So facial animation, eye setup, mouth/lip data, and include-model composition are still outside the current implementation.

7. VTX-side data needed for robust skeletal import is still incomplete. The vendored VTX parser still has TODOs for material replacement lists and bone-related strip-group data in VTX.cpp, VTX.cpp, and VTX.cpp. Even ignoring materials, that bone-path work matters if you want a correct skinned importer.

The short version is: the biggest remaining items are real skeletal/skinned import, bone-controller/runtime control support, and facial systems, plus any remaining advanced Source control paths beyond the currently implemented dependency graph. Mesh metadata, hitboxes, attachments, sequence descriptors, external animblocks, and substantially richer runtime sequence playback are already in decent shape.

If you want, I can turn this into a concrete implementation order next and rank the remaining work by impact versus complexity.