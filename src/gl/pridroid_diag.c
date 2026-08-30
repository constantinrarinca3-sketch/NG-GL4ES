#include "pridroid_diag.h"

#include <string.h>

#include "gl4es.h"
#include "glstate.h"

typedef struct {
    uint64_t api_draw_calls;
    uint64_t api_draw_vertices;
    uint64_t display_list_calls;
    uint64_t driver_draw_calls;
    uint64_t driver_draw_vertices;
    uint64_t clear_calls;
    uint64_t blit_calls;
    uint32_t last_draw_mode;
    uint32_t last_clear_mask;
} pridroid_ng_diag_counters;

static pridroid_ng_diag_counters g_pridroid_diag;

#define RD_ATOMIC_ADD(field, value) \
    __atomic_fetch_add(&g_pridroid_diag.field, (value), __ATOMIC_RELAXED)
#define RD_ATOMIC_STORE(field, value) \
    __atomic_store_n(&g_pridroid_diag.field, (value), __ATOMIC_RELAXED)
#define RD_ATOMIC_LOAD(field) \
    __atomic_load_n(&g_pridroid_diag.field, __ATOMIC_RELAXED)

static uint64_t pridroid_diag_vertices(GLsizei count, GLsizei instances) {
    if (count <= 0 || instances <= 0) return 0;
    return (uint64_t)(uint32_t)count * (uint64_t)(uint32_t)instances;
}

void pridroid_ng_diag_api_draw(GLenum mode, GLsizei count, GLsizei instances) {
    RD_ATOMIC_ADD(api_draw_calls, 1);
    RD_ATOMIC_ADD(api_draw_vertices, pridroid_diag_vertices(count, instances));
    RD_ATOMIC_STORE(last_draw_mode, (uint32_t)mode);
}

void pridroid_ng_diag_driver_draw(GLenum mode, GLsizei count, GLsizei instances) {
    RD_ATOMIC_ADD(driver_draw_calls, 1);
    RD_ATOMIC_ADD(driver_draw_vertices, pridroid_diag_vertices(count, instances));
    RD_ATOMIC_STORE(last_draw_mode, (uint32_t)mode);
}

void pridroid_ng_diag_display_list(void) {
    RD_ATOMIC_ADD(display_list_calls, 1);
}

void pridroid_ng_diag_clear(GLbitfield mask) {
    RD_ATOMIC_ADD(clear_calls, 1);
    RD_ATOMIC_STORE(last_clear_mask, (uint32_t)mask);
}

void pridroid_ng_diag_blit(void) {
    RD_ATOMIC_ADD(blit_calls, 1);
}

int pridroid_ng_diag_snapshot_v1(void* out, size_t out_size) {
    if (!out || out_size < sizeof(pridroid_ng_diag_v1)) return 0;

    pridroid_ng_diag_v1 s;
    memset(&s, 0, sizeof(s));
    s.abi_version = PRIDROID_NG_DIAG_ABI_V1;
    s.struct_size = sizeof(s);
    s.api_draw_calls = RD_ATOMIC_LOAD(api_draw_calls);
    s.api_draw_vertices = RD_ATOMIC_LOAD(api_draw_vertices);
    s.display_list_calls = RD_ATOMIC_LOAD(display_list_calls);
    s.driver_draw_calls = RD_ATOMIC_LOAD(driver_draw_calls);
    s.driver_draw_vertices = RD_ATOMIC_LOAD(driver_draw_vertices);
    s.clear_calls = RD_ATOMIC_LOAD(clear_calls);
    s.blit_calls = RD_ATOMIC_LOAD(blit_calls);
    s.last_draw_mode = RD_ATOMIC_LOAD(last_draw_mode);
    s.last_clear_mask = RD_ATOMIC_LOAD(last_clear_mask);

    extern long zomdroid_fbo_bind_app;
    extern long zomdroid_fbo_bind_native;
    extern long zomdroid_fbo_bind_skip;
    s.fbo_app_binds = (uint64_t)__atomic_load_n(&zomdroid_fbo_bind_app, __ATOMIC_RELAXED);
    s.fbo_native_binds = (uint64_t)__atomic_load_n(&zomdroid_fbo_bind_native, __ATOMIC_RELAXED);
    s.fbo_skipped_binds = (uint64_t)__atomic_load_n(&zomdroid_fbo_bind_skip, __ATOMIC_RELAXED);

    // The bridge calls us from the presenting GL thread immediately before swap.
    // Reading NG's current wrapper state here is therefore both cheap and more useful
    // than another stream of per-state-change log messages.
    if (glstate) {
        s.wrapper_state_valid = 1;
        s.wrapper_program = glstate->glsl ? (int32_t)glstate->glsl->program : -1;
        s.wrapper_draw_fbo = glstate->fbo.fbo_draw ? (int32_t)glstate->fbo.fbo_draw->id : -1;
        s.wrapper_read_fbo = glstate->fbo.fbo_read ? (int32_t)glstate->fbo.fbo_read->id : -1;
        s.wrapper_viewport[0] = glstate->raster.viewport.x;
        s.wrapper_viewport[1] = glstate->raster.viewport.y;
        s.wrapper_viewport[2] = glstate->raster.viewport.width;
        s.wrapper_viewport[3] = glstate->raster.viewport.height;
        s.wrapper_scissor[0] = glstate->raster.scissor.x;
        s.wrapper_scissor[1] = glstate->raster.scissor.y;
        s.wrapper_scissor[2] = glstate->raster.scissor.width;
        s.wrapper_scissor[3] = glstate->raster.scissor.height;
        memcpy(s.wrapper_color_mask, glstate->colormask, sizeof(s.wrapper_color_mask));
        s.wrapper_blend = glstate->enable.blend;
        s.wrapper_depth_test = glstate->enable.depth_test;
        s.wrapper_stencil_test = glstate->enable.stencil_test;
        s.wrapper_cull_face = glstate->enable.cull_face;
    }

    memcpy(out, &s, sizeof(s));
    return 1;
}

