# Official Guide Notes

This file condenses the useful parts of Godot's official `Custom modules in C++` guide into repo-focused notes for this skill.

## When A Module Is The Right Tool

- Prefer a built-in module when you need deep engine integration, editor integration, custom servers/loaders, or other engine-level hooks that are awkward or impossible with GDExtension.
- Prefer GDExtension for isolated functionality that does not need rebuilding the engine after each change.

## Minimal Module Layout

The smallest useful module is typically:

```text
modules/<name>/
  SCsub
  config.py
  register_types.h
  register_types.cpp
  <type>.h
  <type>.cpp
```

- `register_types.h` and `register_types.cpp` must live at the module root next to `SCsub` and `config.py`.
- The `<name>` inside `initialize_<name>_module()` and `uninitialize_<name>_module()` must match the folder name.

Example:

```text
modules/summator/
  SCsub
  config.py
  register_types.h
  register_types.cpp
  summator.h
  summator.cpp
```

## Exposing A Type To Scripts

For a normal class exposed to Godot and GDScript:

1. Inherit from an engine base such as `Object`, `RefCounted`, `Resource`, or `Node`. For a fuller rundown of common choices such as `Node2D`, `Node3D`, and when to prefer them, use `./base-class-guide.md`.
2. Add `GDCLASS(MyType, BaseType)` in the class declaration.
3. Implement `static void _bind_methods()`.
4. Bind methods with `ClassDB::bind_method(D_METHOD(...), &MyType::method)`.
5. Bind properties, signals, constants, and enums there as needed.
6. Register the type from `register_types.cpp` at the right initialization level.

If `_bind_methods()` is missing or incomplete, the class may compile and register, but its scripting API will be partial or unusable.

Example:

```cpp
class Summator : public RefCounted {
  GDCLASS(Summator, RefCounted);

  int count = 0;

protected:
  static void _bind_methods();

public:
  void add(int p_value);
  void reset();
  int get_total() const;
};

void Summator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("add", "value"), &Summator::add);
  ClassDB::bind_method(D_METHOD("reset"), &Summator::reset);
  ClassDB::bind_method(D_METHOD("get_total"), &Summator::get_total);
}
```

## Registering Types In `register_types.cpp`

- Use `initialize_<name>_module(ModuleInitializationLevel p_level)` as the runtime entry point for the module.
- Return early unless `p_level` matches the layer where the type belongs.
- Common patterns:
  - `GDREGISTER_CLASS(MyType)` for instantiable types.
  - `GDREGISTER_ABSTRACT_CLASS(MyBaseType)` for abstract visible bases.
  - `ClassDB::register_custom_instance_class<T>()` for special construction paths used by some built-in engine types.
- Use `uninitialize_<name>_module()` to remove loaders, singletons, or other runtime state that needs teardown.

Example:

```cpp
#include "register_types.h"

#include "core/object/class_db.h"
#include "summator.h"

void initialize_summator_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  GDREGISTER_CLASS(Summator);
}

void uninitialize_summator_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}
```

## Choosing The Initialization Level

- `MODULE_INITIALIZATION_LEVEL_CORE`: low-level core functionality.
- `MODULE_INITIALIZATION_LEVEL_SERVERS`: server-level systems.
- `MODULE_INITIALIZATION_LEVEL_SCENE`: most scene-facing classes, resources, and nodes.
- `MODULE_INITIALIZATION_LEVEL_EDITOR`: editor-only plugins and tools.

If unsure, start from the closest built-in module archetype rather than guessing.

## `config.py` Responsibilities

`config.py` is build metadata, not runtime type registration.

- `can_build(env, platform)`: whether the module is allowed on the current target.
- `configure(env)`: extra SCons-side configuration when needed.
- `get_opts(platform)`: custom SCons options.
- `is_enabled()`: default-off modules.
- `env.module_add_dependencies(...)`: module dependencies.
- `get_doc_path()` and `get_doc_classes()`: module doc ownership.
- `get_icons_path()`: custom icon directory if not using the default `icons/` path.

Example:

```python
def can_build(env, platform):
  return True


def configure(env):
  pass


def get_doc_classes():
  return [
    "Summator",
  ]


def get_doc_path():
  return "doc_classes"
```

## `SCsub` Responsibilities

- Add source files to `env.modules_sources`.
- Clone the environment before adding module-specific flags so the whole engine build is not polluted.
- Add include paths with `CPPPATH`.
- Add compile flags with `CCFLAGS`, `CFLAGS`, or `CXXFLAGS` on the cloned environment.
- Keep thirdparty, platform-specific, and editor-only logic local to the module.

Example:

```python
Import("env")

module_env = env.Clone()
module_env.add_source_files(env.modules_sources, "*.cpp")
module_env.Append(CPPPATH=["#thirdparty/mylib/include"])
module_env.Append(CCFLAGS=["-O2"])
```

## Using The Module From GDScript

After rebuilding the engine with the module enabled:

```gdscript
var s := Summator.new()
s.add(10)
s.add(20)
print(s.get_total())
```

- `Node`-derived types appear in the editor's Add Node dialog.
- `Resource`-derived types appear in resource workflows and their exposed properties can be serialized.
- Avoid multiple inheritance on classes exposed with `GDCLASS`.

Example:

```gdscript
var s := Summator.new()
s.add(10)
s.add(20)
s.add(30)
print(s.get_total())
s.reset()
```

## Docs, Tests, And Icons

- Docs:
  - Add `doc_classes/` in the module root.
  - List all module-owned classes in `get_doc_classes()`.
  - Point `get_doc_path()` at that directory.
  - Generate XML with `bin/<godot_binary> --doctool .` and edit the generated files.
  - If a class is not listed, documentation may fall back into `doc/classes/`.
  - For core or other non-module classes, reference XML lives under `doc/classes/<ClassName>.xml`.
- Tests:
  - Add `tests/test_<name>.h` under the module.
  - The `test_` prefix matters because the build system collects these headers.
  - Build with `scons tests=yes` and run with `--test`.
- Icons:
  - Put SVG icons under `icons/` by default.
  - Override with `get_icons_path()` when needed.

Example module docs layout:

```text
modules/summator/
  config.py
  doc_classes/
    Summator.xml
```

Example core docs layout:

```text
doc/classes/
  Engine.xml
  Node.xml
```

Example test header:

```cpp
#pragma once

#include "modules/summator/summator.h"
#include "tests/test_macros.h"

namespace TestSummator {

TEST_CASE("[Modules][Summator] Adding numbers") {
	Ref<Summator> s = memnew(Summator);
	CHECK(s->get_total() == 0);
	s->add(10);
	CHECK(s->get_total() == 10);
}

} // namespace TestSummator
```

## Build And Distribution Notes

- For platform-specific compile setup and command patterns, use `./compilation-guide.md`.
- If the module is used by the running game, rebuild every export template that needs it, not just the editor binary.
- For an out-of-tree module collection, SCons can build modules from another directory with `custom_modules=<path>`.
- Built-in modules placed directly under `modules/` are auto-discovered; out-of-tree modules are distinguished by absolute paths internally, which can affect doc-generation assumptions.

## Preregister Hooks

- Some modules need work before normal module initialization.
- In this codebase, preregister support is declared with `#define MODULE_<NAME>_HAS_PREREGISTER` in `register_types.h`.
- Use this only when the module genuinely needs earlier setup than normal `initialize_<name>_module()` timing.

## Practical Rule

Start from the closest real module in `modules/`, not from the toy `summator` example. Use the official guide for the minimal contract, and use nearby engine modules for the exact conventions this repository already follows.