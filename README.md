Zomdroid / Project Zomboid Build 42 adaptation (branch `zomdroid-base-june16`)
====

> This fork branch adapts **NG-GL4ES** for [Zomdroid](https://github.com/udarmolota/zomDroid) —
> running **Project Zomboid Build 42** on Android devices **without usable Vulkan**
> (MediaTek / Mali, PowerVR and other GLES-only GPUs, where the ZINK renderer is not
> an option).

During June–July 2026 this branch fixed the complete chain that kept PZ B42 unplayable
on pure GLES:

* **World rendered white/empty** — GLSL uniform initializers (`uniform int useTexture = 1;`)
  were stripped by the converter without being recorded/restored; the whole
  record→merge→restore pipeline was rebuilt (typed declarations, correct ordering,
  value parser for `vecN(...)` constructors and `a / b` expressions).
* **Invisible characters/zombies/vehicles** — PZ links several compilation units per
  shader stage (desktop-GL habit, illegal in GLES) and redefines `max/min/clamp`;
  fixed with builtin-redefinition stripping + the game's own `combineShaderSources`
  mode (one-byte `ShaderUnit.class` patch, per game version).
* **World draws silently dropped** — client-side index draws executed while a native
  element buffer was bound (`GL_INVALID_OPERATION` on every world tile).
* **Stencil-clipped UI shredded/empty** (inventory, menus) — `glClear` forced the
  stencil write mask to `0x1` instead of `0xFF` and never restored it; also the FPE
  alpha-test customization discarded ColorMask-0000 stencil-write quads.
* **Vehicles/water/blur link failures on Adreno** — strict ESSL has no implicit
  int→float conversions; `const float x = <int expr>` and `clamp(x, 0, 1)` idioms are
  now legalized at source level.

Each fix is documented in its commit message with the root-cause analysis. Upstream
(BZLZHH/aaaapai) does **not** contain these fixes as of July 2026 — PR candidates.

Original README below.

---

Krypton Wrapper
====

> [!NOTE]
> 
> The ORIGINAL project may be no longer maintained.

> [!NOTE]
> 
> The latest version:
> 
> **Release 0.4.1**
>
> See [Releases](https://github.com/BZLZHH/NG-GL4ES/releases)

**Krypton Wrapper** *(also called Next Generation GL4ES/NG-GL4ES)* is a fork from [gl4es](https://github.com/ptitSeb/gl4es) and [gl4es-114-extra](https://github.com/PojavLauncherTeam/gl4es-114-extra). 

This project is making it support more OpenGL functions.

Features
====

1. Be able to run Minecraft [Sodium](https://github.com/CaffeineMC/sodium) mod;

2. Be able to render some minecraft shaders (with or without realtime shadows) with a high efficiency with Minecraft [Iris](https://github.com/IrisShaders/Iris) mod;

3. Be able to run Minecraft in most versions (like Minecraft 1.21.8 / Minecraft 1.12.2).

Change Log
===

Please see [CHANGELOG.md](https://github.com/BZLZHH/NG-GL4ES/blob/main/CHANGELOG.md)

Build
====

Please build with CMakeLists.txt.

License
====

[gl4es](https://github.com/ptitSeb/gl4es): MIT License

[gl4es-114-extra](https://github.com/PojavLauncherTeam/gl4es-114-extra): MIT License

This Project (for modified code): MIT License

Please see [LICENSE](https://github.com/BZLZHH/NG-GL4ES/blob/main/LICENSE).

Third party components
====

**SPIRV-Cross** by **KhronosGroup** - [Apache License 2.0](https://github.com/KhronosGroup/SPIRV-Cross/blob/master/LICENSE): [github](https://github.com/KhronosGroup/SPIRV-Cross)

**glslang** by **KhronosGroup** - [Various Licenses](https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt): [github](https://github.com/KhronosGroup/glslang)

**cJSON** by **DaveGamble** - [MIT License](https://github.com/DaveGamble/cJSON/blob/master/LICENSE): [github](https://github.com/DaveGamble/cJSON)

Some code is from MobileGlues.

Sponsor
====

**ptitSeb (gl4es)**: [paypal](https://paypal.me/0ptitSeb)

**PojavLauncherTeam (gl4es-114-extra)**: [patreon](https://patreon.com/pojavlauncher)
