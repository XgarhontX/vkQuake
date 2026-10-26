#ifndef R_BLOOMHDR_H
#define R_BLOOMHDR_H

extern cvar_t					r_bloomhdr;
extern cvar_t					r_bloomhdr_intensity;
extern cvar_t					r_bloomhdr_threshold;
extern cvar_t					r_bloomhdr_mips;

extern vulkan_pipeline_layout_t bloomhdr_fragment_layout;
extern vulkan_pipeline_t bloomhdr_fragment_pipelines[MAIN_RENDER_PASS_VARIANT_COUNT];
extern vulkan_pipeline_layout_t bloomhdr_compute_layout;
extern vulkan_pipeline_t bloomhdr_compute_pipeline_downfilter;
extern vulkan_pipeline_t bloomhdr_compute_pipeline_down;
extern vulkan_pipeline_t bloomhdr_compute_pipeline_up;
extern vulkan_pipeline_t bloomhdr_compute_pipeline_upfilter;

#define BLOOMHDR_MAX_MIPS 8
#define BLOOMHDR_MIN_MIPS 3

typedef struct
{
	vec2_t src_size;
	vec2_t inv_src_size;
	vec2_t dst_size;
	vec2_t inv_dst_size;
	float  intensity;
	float  threshold;
	float  p0;
	float  p1;
} bloomhdr_constants_t;

void R_InitBloomHDR (void);
void R_CreateBloomHDR (const VkImageView scene_views[NUM_COLOR_BUFFERS]);
void R_DestroyBloomHDR (void);
bool R_BloomHDRMipsChanged (void);

void R_ComputeBloomHDR (cb_context_t *cbx);
void R_DrawBloomHDRTask (void *unused);

#endif

