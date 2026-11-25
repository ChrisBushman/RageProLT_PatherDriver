/* gld_texture.c - texture upload/bind and the per-draw state translation.  Stub for the first cut: untextured drawing only. */
#include "gld.h"

int mach64_fog_on, mach64_spec_mode, mach64_flat_on;
uint32_t mach64_tex_vofs[2];

int mach64_bind_texture(gld_context *ctx) { (void)ctx; mach64_tex_vofs[0] = 0x80; return 0; }
void mach64_tex_select_lod(gld_context *ctx, float pix_area, float uv_area) { (void)ctx; (void)pix_area; (void)uv_area; }
void mach64_texture_dirtied(void *t) { (void)t; }
void mach64_texture_bound(void *t) { (void)t; }
void mach64_texture_deleted(void *t) { (void)t; }
