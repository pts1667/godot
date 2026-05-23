---
name: godot-modules
description: 'Create or edit built-in Godot engine modules under modules/. Use when adding a new module, updating SCsub, config.py, register_types.cpp or register_types.h, wiring docs or tests, or adapting platform-specific, editor-only, or thirdparty-backed module code.'
argument-hint: '<module name or task>'
user-invocable: true
---

# Godot Modules

Use this skill for built-in engine modules under `modules/<name>/`.

For the broader official module workflow, including minimal module layout, doctool, tests, icons, preregister hooks, export-template rebuilds, and out-of-tree `custom_modules` builds, use `./references/official-guide-notes.md`.
For choosing a practical base class for a new engine type, use `./references/base-class-guide.md`.
For platform-aware build and validation commands, use `./references/compilation-guide.md`.

## Module Architecture

- `SCsub`: build wiring for the module. It adds the module's `.cpp` files to `env.modules_sources`, applies module-local defines and include paths, gates editor-only or platform-specific code, and compiles thirdparty sources in a separate cloned environment when needed. Normal modules are auto-discovered by the top-level `modules/SCsub`.
- `register_types.h`: declares `initialize_<name>_module()` and `uninitialize_<name>_module()`. The generated module registrar calls these for enabled modules; you do not edit the generated registrars directly.
- `register_types.cpp`: runtime registration point. This is where the module exposes engine types, singletons, project settings, resource loaders, editor plugins, or servers at the correct `ModuleInitializationLevel`.
- `config.py`: SCons metadata for the module. It controls whether the module can build, which SCons options it adds, whether it is default-on or default-off, which other modules it depends on, and which doc classes belong to it. It does not register runtime types.

## Registering New Types

1. Add the C++ type in the module, usually inheriting `Object`, `RefCounted`, `Resource`, or `Node`.
  Choose the closest fit from `./references/base-class-guide.md` instead of defaulting to `Object`.
2. In the class header, use `GDCLASS(MyType, BaseType)`.
3. Bind the API in `_bind_methods()` with `ClassDB::bind_method`, `ADD_PROPERTY`, signals, and constants. Without binding, the class may exist in C++ but will not expose a usable script API.
4. Include the type header from `register_types.cpp`.
5. In `initialize_<name>_module()`, gate by the right `ModuleInitializationLevel` and register the type:
  - `GDREGISTER_CLASS(MyType)` for normal instantiable classes.
  - `GDREGISTER_ABSTRACT_CLASS(MyBaseType)` for abstract bases that should be visible but not instantiated.
  - `ClassDB::register_custom_instance_class<T>()` only when the class needs custom engine-side construction and a nearby module already uses that pattern.
6. Keep `uninitialize_<name>_module()` symmetrical if the module installs loaders, singletons, or other teardown-sensitive state.

## How The Type Reaches GDScript

- After the type is bound and registered, it is exposed through `ClassDB` and becomes usable from scripts once the engine is rebuilt with the module enabled.
- Instantiable classes can usually be created in GDScript with `MyType.new()`.
- The type can then be used in annotations, exports, `is` checks, and return types.
- Abstract classes and some custom-instance classes are not created with `.new()`; they are typically returned by engine APIs or factories.

```gdscript
var reader := ZIPReader.new()
var peer: WebRTCPeerConnection

if reader is ZIPReader:
	pass
```

## What `config.py` Is For

- `can_build(env, platform)`: hard gate for whether the module participates in the build on the current target.
- `configure(env)`: extra build-time configuration when needed.
- `get_opts(platform)`: declares module-specific SCons options.
- `is_enabled()`: makes the module default-off when needed.
- `env.module_add_dependencies(...)` inside `can_build(...)`: declares module dependencies.
- `get_doc_classes()` and `get_doc_path()`: tell the doc build which classes belong to this module.

## Workflow

1. Start with the target module or the closest archetype from `./references/module-archetypes.md`.
2. Pull in `./references/official-guide-notes.md` when you need the official minimal layout, build/distribution edge cases, docs/tests/icons guidance, or a refresher on script exposure.
3. Edit only the owning files: `SCsub`, `config.py`, `register_types.*`, source files, and `doc_classes/` if public API changed.
4. Keep public classes documented and add test hooks when the module exposes new public behavior.
5. Do not hand-edit generated files such as `modules/modules_enabled.gen.h` or `modules/register_module_types.gen.cpp`.

## Validation

- Check naming consistency across `modules/<name>/`, `register_types.*`, docs, and any `module_<name>_enabled` toggle.
- Run the narrowest practical build, for example `scons platform=<platform> target=editor module_<name>_enabled=yes`.
- If tests changed, use `tests=yes`.
- If the module is default-off or platform-gated, validate that branch explicitly.
- Use `./references/compilation-guide.md` when the right platform/toolchain/build flags are not obvious.