#ifndef _PRIDROID_NG_DIAG_H_
#define _PRIDROID_NG_DIAG_H_

#include <stddef.h>
#include <stdint.h>

#include "attributes.h"
#include "gles.h"

// Small, versioned ABI consumed by PriDroid's final eglSwapBuffers bridge.  Keep
// this POD-only: the launcher deliberately does not include NG's internal headers.
#define PRIDROID_NG_DIAG_ABI_V1 1u

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    uint64_t api_draw_calls;
    uint64_t api_draw_vertices;
    uint64_t display_list_calls;
    uint64_t driver_draw_calls;
    uint64_t driver_draw_vertices;
    uint64_t clear_calls;
    uint64_t blit_calls;

    uint64_t fbo_app_binds;
    uint64_t fbo_native_binds;
    uint64_t fbo_skipped_binds;

    uint32_t last_draw_mode;
    uint32_t last_clear_mask;
    int32_t wrapper_state_valid;
    int32_t wrapper_program;
    int32_t wrapper_draw_fbo;
    int32_t wrapper_read_fbo;
    int32_t wrapper_viewport[4];
    int32_t wrapper_scissor[4];
    uint8_t wrapper_color_mask[4];
    uint8_t wrapper_blend;
    uint8_t wrapper_depth_test;
    uint8_t wrapper_stencil_test;
    uint8_t wrapper_cull_face;
} pridroid_ng_diag_v1;

void pridroid_ng_diag_api_draw(GLenum mode, GLsizei count, GLsizei instances);
void pridroid_ng_diag_driver_draw(GLenum mode, GLsizei count, GLsizei instances);
void pridroid_ng_diag_display_list(void);
void pridroid_ng_diag_clear(GLbitfield mask);
void pridroid_ng_diag_blit(void);

// Returns 1 when a complete V1 snapshot was written, 0 for an incompatible buffer.
EXPORT int pridroid_ng_diag_snapshot_v1(void* out, size_t out_size);

#endif
