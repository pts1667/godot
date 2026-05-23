# Compilation Guide

Use this file when you need to compile or validate Godot after changing a built-in module.

This guide is system-independent in structure, with Windows and Linux/*BSD sections where the commands differ.

## Quick Defaults

- For Windows, prefer MSVC unless the user explicitly asks for MinGW or LLVM.
- For Linux/*BSD, use GCC by default; use Clang when required by the platform or when the user asks for it.
- For module iteration, start with an editor build, then add tests or export-template builds only when the change needs them.

## What You Usually Build

- Editor validation build: checks that the engine and module compile and the type is available in the editor.
- Test build: needed when module tests under `tests/` changed.
- Export templates: needed when the module is used at runtime outside the editor.

## Common SCons Patterns

Use these as the base shape, then add platform and feature options as needed.

```text
scons platform=<platform> target=editor
scons platform=<platform> target=editor module_<name>_enabled=yes
scons platform=<platform> target=editor tests=yes module_<name>_enabled=yes
scons platform=<platform> target=template_debug module_<name>_enabled=yes
scons platform=<platform> target=template_release module_<name>_enabled=yes
```

Helpful flags:

- `dev_build=yes` or `dev_mode=yes`: better default choice for engine development.
- `production=yes`: optimized production build.
- `arch=<arch>`: target architecture such as `x86_64`, `x86_32`, or `arm64` where supported.
- `use_llvm=yes`: use Clang/LLVM toolchain.

## Windows

### Toolchain And Prerequisites

- Preferred compiler: Visual Studio 2019+ with C++ support. Visual Studio 2022 is the recommended default.
- Also required: Python 3.9+ and SCons 4.4+.
- Build from `cmd.exe` or PowerShell when using MSVC. Do not use MSVC from MSYS2 or MinGW shells.

### Default Build

```powershell
scons platform=windows target=editor dev_build=yes
```

### Module Validation Examples

```powershell
scons platform=windows target=editor dev_build=yes module_my_module_enabled=yes
scons platform=windows target=editor dev_build=yes tests=yes module_my_module_enabled=yes
```

### Toolchain Variants

- MSVC default: no extra flags if Visual Studio is installed.
- MinGW-w64: add `use_mingw=yes`.
- MinGW-LLVM: add `use_mingw=yes use_llvm=yes`.

Example:

```powershell
scons platform=windows target=editor use_mingw=yes module_my_module_enabled=yes
```

### Useful Notes

- Default Windows builds include Direct3D 12 support; use `d3d12=no` if you intentionally want to skip that dependency setup.
- For contribution or iteration builds, `dev_build=yes` is usually the right default.
- For optimized builds, use `production=yes`. This can significantly increase RAM usage when LTO is enabled.
- Create `_sc_` or `._sc_` in `bin/` for self-contained editor settings.

### Export Templates

If your module affects runtime behavior, build matching templates as well:

```powershell
scons platform=windows target=template_debug arch=x86_64 module_my_module_enabled=yes
scons platform=windows target=template_release arch=x86_64 module_my_module_enabled=yes
```

Also build any additional architectures you need, such as `x86_32` or `arm64`.

## Linux and *BSD

### Toolchain And Prerequisites

- Default compiler: GCC 9+.
- Alternative compiler: Clang 6+ with `use_llvm=yes`.
- Also required: Python 3.9+, SCons 4.4+, `pkg-config`, and platform development libraries such as X11/Wayland, Mesa, ALSA, and PulseAudio.
- Use `platform=linuxbsd` for Godot 4.x.

### Default Build

```bash
scons platform=linuxbsd target=editor dev_build=yes
```

### Module Validation Examples

```bash
scons platform=linuxbsd target=editor dev_build=yes module_my_module_enabled=yes
scons platform=linuxbsd target=editor dev_build=yes tests=yes module_my_module_enabled=yes
```

### Toolchain Variants

- Clang: add `use_llvm=yes`.
- Faster linking during development: optionally add `linker=lld` or `linker=mold` when supported.

Example:

```bash
scons platform=linuxbsd target=editor dev_build=yes use_llvm=yes linker=lld module_my_module_enabled=yes
```

### Useful Notes

- GCC is the safer default for normal Linux development.
- Clang is useful on platforms that require it, such as OpenBSD, or when targeting RISC-V.
- `production=yes` enables stronger optimization and LTO; if memory is tight, reduce LTO with `lto=none` or `lto=thin`.
- Using system libraries can improve link times, but reduces portability and should not be the default for distributable builds.

### Headless And Server Builds

- Headless editor workflow: build the normal editor and run it with `--headless`.
- Dedicated server-oriented builds:

```bash
scons platform=linuxbsd target=template_debug module_my_module_enabled=yes
scons platform=linuxbsd target=template_release production=yes module_my_module_enabled=yes
```

### Export Templates

If your module affects runtime behavior, build matching templates as well:

```bash
scons platform=linuxbsd target=template_debug arch=x86_64 module_my_module_enabled=yes
scons platform=linuxbsd target=template_release arch=x86_64 module_my_module_enabled=yes
```

## When To Rebuild Templates

Rebuild export templates when:

- The module exposes runtime classes used by the game.
- The module changes runtime behavior, loaders, resources, rendering, audio, networking, or anything else used outside the editor.

An editor-only module change does not always require new runtime templates, but confirm that the change is truly editor-only before skipping them.

## Practical Validation Order

1. Build the editor for the current platform with the module enabled.
2. If tests changed, rebuild with `tests=yes` and run the affected tests.
3. If the module is used by the running game, build matching export templates.
4. If the module is platform-gated, validate the intended platform/toolchain branch directly.

## Out-Of-Tree Modules

For separate module directories, SCons can discover them with `custom_modules=<path>`.

Example:

```text
scons platform=<platform> custom_modules=../modules
```

This is mainly relevant for custom external modules; built-in modules under `modules/` are discovered automatically.