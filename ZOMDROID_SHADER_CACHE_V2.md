# Zomdroid shader-conversion cache v2

This experimental branch repairs the opt-in `LIBGL_CONVCACHE=1` path used by
Project Zomboid Build 42.

The warm-launch failure had two cache-only causes:

1. The tables behind `hasBuiltinAttrib()` were initialized only by the conversion
   functions. An all-hit launch bypassed conversion, so every table entry remained
   an empty string. `strstr(shader, "")` then matched every attribute and the linker
   received empty names through `glBindAttribLocation`, making otherwise valid
   compiled shaders fail to link with an empty driver log.
2. The original cache restored converted GLSL text without restoring
   `shaderconv_need_t`, so later vertex/fragment compatibility decisions were made
   from zeroed metadata.

The first issue explains the observed cold/mixed-cache pass followed by an
all-cache black screen. The second is a separate correctness defect that could
break other shader pairs even after the table initialization was fixed.

V2 adds:

- conversion-independent initialization of the built-in attribute-name tables;
- a versioned, stage- and capability-specific cache key and header;
- field-by-field persistence of `shaderconv_need_t`;
- validation of the original source and a payload checksum;
- caller-controlled storage through `NGG_DIR_PATH`;
- one cache-bypassed cold conversion retry after a cache-assisted link failure;
- persistent quarantine (`.rejected`) so a failed entry is not recreated next run.

The cache remains opt-in. Set `NGG_DIR_PATH` to an application-private directory
and `LIBGL_CONVCACHE=1`. Old `cc_*.bin` entries are ignored because V2 uses the
`zcc2_*.bin` prefix.

## Device validation

Use the same renderer settings, game instance, save, and device throughout:

1. clear the cache directory and launch the world once;
2. fully stop the application;
3. launch the same world three more times without clearing the cache;
4. verify non-zero `CCACHE session: hits`, successful program links, and the
   absence of `CCACHE link fallback` under normal operation.

If the fallback appears, the game should continue after one cold retry. Preserve
the native log and the rejected `zcc2_*.bin` set for analysis.
