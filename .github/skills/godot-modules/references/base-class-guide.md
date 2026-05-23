# Base Class Guide

Use this file when deciding what a new module type should inherit from.

The base class determines lifetime rules, whether the type lives in the scene tree, whether it is saved as a resource, whether it appears in editor node creation dialogs, and how scripts will typically use it.

## Quick Rule

- Pick the narrowest useful base class that already matches the behavior you want.
- Do not default to `Object` if the type is really resource-like or scene-like.
- Before inventing a new inheritance shape, look for a nearby built-in type with similar behavior.

## Common Choices

### `Object`

What it gives you:

- The root Godot object model: methods, properties, signals, notifications, metadata, and script attachment.
- Explicit lifetime management with `free()` unless a subclass adds automatic ownership.

Use it for:

- Low-level engine objects that are not scene nodes and not saved as resources.
- Types that mostly expose API surface and callbacks, with lifetime managed elsewhere.

What you can do with it:

- Register methods, properties, and signals for scripting.
- Handle notifications and dynamic property access.

Local docs: `doc/classes/Object.xml`

### `RefCounted`

What it gives you:

- Everything from `Object`, plus automatic lifetime via reference counting.
- Script-friendly helper objects that usually do not need manual `free()`.

Use it for:

- Handles, readers, parsers, peers, wrappers, and small helper APIs returned by engine code.

What you can do with it:

- Return objects from APIs and let scripts hold them safely.
- Share helper instances without managing deletion manually.

Watch for:

- Cyclic references can still leak.

Local docs: `doc/classes/RefCounted.xml`

### `Resource`

What it gives you:

- Everything from `RefCounted`, plus serialization, duplication, path-based loading, and resource-cache behavior.
- Natural inspector and file-based workflows.

Use it for:

- Data assets, import results, reusable authored data, and anything users should save as `.tres` or embed in scenes.

What you can do with it:

- Save and load instances from disk.
- Expose editable data in inspector workflows.
- Nest it inside scenes and other resources.

Local docs: `doc/classes/Resource.xml`

### `Node`

What it gives you:

- Everything from `Object`, plus scene-tree membership, parent/child hierarchy, ownership, groups, RPC, and lifecycle callbacks like `_enter_tree`, `_ready`, `_process`, and `_physics_process`.
- Child ownership semantics: freeing the node frees its children.

Use it for:

- Runtime behavior objects that users add to scenes.
- Managers, controllers, emitters, and systems that need tree lifecycle or per-frame processing.

What you can do with it:

- Compose behavior hierarchically in scenes.
- Receive tree and process callbacks.
- Participate in groups, RPC, and node-path based scene logic.

Local docs: `doc/classes/Node.xml`

### `Node2D`

What it gives you:

- Everything from the 2D scene stack, plus 2D transform state such as position, rotation, scale, skew, and draw-order related behavior.

Use it for:

- 2D gameplay objects, markers, helpers, and editor-visible 2D scene types.

What you can do with it:

- Move, rotate, and scale the node in 2D space.
- Build 2D scene objects that users place directly in 2D scenes.

Local docs: `doc/classes/Node2D.xml`

### `Node3D`

What it gives you:

- Everything from `Node`, plus 3D transform state and 3D world participation.

Use it for:

- Spatial runtime objects, 3D helpers, and editor-visible 3D scene types.

What you can do with it:

- Position, rotate, and scale objects in 3D space.
- Attach the type naturally to 3D scenes and tools.

Local docs: `doc/classes/Node3D.xml`

### `Control`

What it gives you:

- A GUI node with a bounding rect, anchors, offsets, focus behavior, theme integration, and `_gui_input` handling.
- Automatic UI layout behavior relative to parent controls and containers.

Use it for:

- Editor panels, docks, inspectors, custom widgets, and runtime UI.

What you can do with it:

- Build interactive UI that participates in Godot's focus, theme, and GUI input systems.
- Use anchors, offsets, and containers for adaptive layouts.

Local docs: `doc/classes/Control.xml`

## Decision Cheatsheet

- Low-level engine object with explicit lifetime: `Object`
- Shared helper object returned by APIs: `RefCounted`
- Saveable authored data or import result: `Resource`
- Scene-tree behavior object: `Node`
- 2D scene object: `Node2D`
- 3D scene object: `Node3D`
- UI/editor panel or widget: `Control`

## Quick Examples

- Archive reader, parser, peer, or other helper API: `RefCounted`
- Saveable settings or imported data blob: `Resource`
- Scene manager or runtime behavior component: `Node`
- 2D gameplay/editor object with transforms: `Node2D`
- 3D gameplay/editor object with transforms: `Node3D`
- Editor dock or custom widget: `Control`

## Practical Rule

- If scripts mostly call `MyType.new()` and keep it as a helper object, `RefCounted` is often right.
- If users save it to `.tres` or embed it into scenes, `Resource` is often right.
- If users add it to the scene tree, inherit from `Node` or a more specific scene class.