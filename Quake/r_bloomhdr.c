// Bloom by pyramid gaussian blur and upsample.
//
// - Scene in gamma -> DownFilter comp -> linear mip 0
// - linear mip 0 -> Down comp -> linear mip n
// - linear mip n -> Up comp -> linear mip 0
// - linear mip 0 -> UpFilter comp -> Scene delta (gamma space)
// - Scene delta -> Composite frag -> Bloom + Scene in gamma, which is additively blended

#include "quakedef.h"
#include "r_bloomhdr.h"

cvar_t					 r_bloomhdr = {"r_bloomhdr", "1", CVAR_ARCHIVE};
cvar_t					 r_bloomhdr_intensity = {"r_bloomhdr_intensity", "1.0", CVAR_ARCHIVE};
cvar_t					 r_bloomhdr_threshold = {"r_bloomhdr_threshold", "0.8", CVAR_ARCHIVE};
cvar_t					 r_bloomhdr_mips = {"r_bloomhdr_mips", "7", CVAR_ARCHIVE};

#define BLOOMHDR_INTENSITYFUDGE ((1.f/effective_mips) * 0.4f)
#define BLOOMHDR_BUFFERFORMAT VK_FORMAT_B10G11R11_UFLOAT_PACK32

// see gl_misc.c R_CreatePipelineLayouts(), R_DestroyPipelineLayouts(), R_CreatePipelines(), R_DestroyPipelines()
vulkan_pipeline_layout_t bloomhdr_fragment_layout;
vulkan_pipeline_t bloomhdr_fragment_pipelines[MAIN_RENDER_PASS_VARIANT_COUNT];
vulkan_pipeline_layout_t bloomhdr_compute_layout;
vulkan_pipeline_t bloomhdr_compute_pipeline_downfilter;
vulkan_pipeline_t bloomhdr_compute_pipeline_down;
vulkan_pipeline_t bloomhdr_compute_pipeline_up;
vulkan_pipeline_t bloomhdr_compute_pipeline_upfilter;

static VkImage working_images[BLOOMHDR_MAX_MIPS];
static VkImageView working_views[BLOOMHDR_MAX_MIPS];
static vulkan_memory_t working_memory[BLOOMHDR_MAX_MIPS];
static VkDescriptorSet working_read[BLOOMHDR_MAX_MIPS];
static VkDescriptorSet working_write[BLOOMHDR_MAX_MIPS];
static uint32_t working_width[BLOOMHDR_MAX_MIPS]; // resolution
static uint32_t working_height[BLOOMHDR_MAX_MIPS]; // resolution
static VkSampler sampler; // main linear sampler

static int effective_mips; // mips calculated for render size
static float r_bloomhdr_mips_prev; // dirty check for vid_restart

// Full resolution scene delta (gamma space), written by the final upsample and blended by the composite.
static VkImage delta_image;
static VkImageView delta_view;
static vulkan_memory_t delta_memory;
static VkDescriptorSet delta_read;
static VkDescriptorSet delta_write;
static VkDescriptorSet scene_read[NUM_COLOR_BUFFERS]; // which one is live depends on screen effects, see R_SceneColorBufferIndex()

void R_InitBloomHDR (void)
{
	Cvar_RegisterVariable (&r_bloomhdr);
	Cvar_RegisterVariable (&r_bloomhdr_intensity);
	Cvar_RegisterVariable (&r_bloomhdr_threshold);
	Cvar_RegisterVariable (&r_bloomhdr_mips);
	r_bloomhdr_mips_prev = r_bloomhdr_mips.value;
}

// r_bloomhdr_mips clamped by the minimum, maximum and what the render size can fit
static int R_BloomHDREffectiveMips (void)
{
	int res_x = vid.render_width / 2;
	int res_y = vid.render_height / 2;
	int mips = BLOOMHDR_MIN_MIPS;
	while (res_x > 1 && res_y > 1 && mips < BLOOMHDR_MAX_MIPS && mips < (int)r_bloomhdr_mips.value)
	{
		res_x >>= 1;
		res_y >>= 1;
		mips++;
	}
	return mips;
}

// see GL_BeginRendering(), this determines vid_restart
bool R_BloomHDRMipsChanged (void)
{
	if (r_bloomhdr.value == 0) return false;
	bool is_mips_changed = r_bloomhdr_mips_prev != r_bloomhdr_mips.value;
	r_bloomhdr_mips_prev = r_bloomhdr_mips.value;
	return is_mips_changed;
}

void R_CreateBloomHDR (const VkImageView scene_views[NUM_COLOR_BUFFERS])
{
	if (r_bloomhdr.value <= 0)
		return;

	effective_mips = R_BloomHDREffectiveMips ();

	const VkSamplerCreateInfo sampler_info = { // sampler
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.maxLod = (float)effective_mips,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler (vulkan_globals.device, &sampler_info, NULL, &sampler) != VK_SUCCESS)
		Sys_Error ("Couldn't create bloom sampler");

	// images: mip chain/pyramid
	for (int i = 0; i < effective_mips; ++i) 
	{
		const int w = q_max (1, (vid.render_width / 2) >> i);
		const int h = q_max (1, (vid.render_height / 2) >> i);
		working_width[i] = w;
		working_height[i] = h;

		const VkImageCreateInfo image_info = { // info
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = BLOOMHDR_BUFFERFORMAT,
			.extent = {w, h, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		};
		if (vkCreateImage (vulkan_globals.device, &image_info, NULL, &working_images[i]) != VK_SUCCESS)
			Sys_Error ("Couldn't create bloom image");

		VkMemoryRequirements req; // req, alloc, bind
		vkGetImageMemoryRequirements (vulkan_globals.device, working_images[i], &req);
		VkMemoryAllocateInfo alloc = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = req.size,
			.memoryTypeIndex = GL_MemoryTypeFromProperties (req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
		};
		R_AllocateVulkanMemory (&working_memory[i], &alloc, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		if (vkBindImageMemory (vulkan_globals.device, working_images[i], working_memory[i].handle, 0) != VK_SUCCESS)
			Sys_Error ("Couldn't bind bloom image memory");

		const VkImageViewCreateInfo view_info = { // views & descriptors
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = working_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = image_info.format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		if (vkCreateImageView (vulkan_globals.device, &view_info, NULL, &working_views[i]) != VK_SUCCESS)
			Sys_Error ("Couldn't create bloom image view");

		working_read[i] = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
		working_write[i] = R_AllocateDescriptorSet (&vulkan_globals.single_texture_cs_write_set_layout);

		const VkDescriptorImageInfo image = {sampler, working_views[i], VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.pImageInfo = &image,
		};
		write.dstSet = working_read[i];
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);

		write.dstSet = working_write[i];
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}

	// images: full resolution scene delta (gamma space) for UpFilter and Composite
	{
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = BLOOMHDR_BUFFERFORMAT,
			.extent = {vid.render_width, vid.render_height, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		};
		if (vkCreateImage (vulkan_globals.device, &image_info, NULL, &delta_image) != VK_SUCCESS)
			Sys_Error ("Couldn't create bloom delta image");

		VkMemoryRequirements req;
		vkGetImageMemoryRequirements (vulkan_globals.device, delta_image, &req);
		VkMemoryAllocateInfo alloc = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = req.size,
			.memoryTypeIndex = GL_MemoryTypeFromProperties (req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
		};
		R_AllocateVulkanMemory (&delta_memory, &alloc, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
		if (vkBindImageMemory (vulkan_globals.device, delta_image, delta_memory.handle, 0) != VK_SUCCESS)
			Sys_Error ("Couldn't bind bloom delta image memory");

		const VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = delta_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = image_info.format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		if (vkCreateImageView (vulkan_globals.device, &view_info, NULL, &delta_view) != VK_SUCCESS)
			Sys_Error ("Couldn't create bloom delta image view");

		delta_read = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
		delta_write = R_AllocateDescriptorSet (&vulkan_globals.single_texture_cs_write_set_layout);

		const VkDescriptorImageInfo image = {sampler, delta_view, VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.pImageInfo = &image,
		};
		write.dstSet = delta_read;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);

		write.dstSet = delta_write;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}

	// DownFilter's sampler to Scene buffers
	for (int i = 0; i < NUM_COLOR_BUFFERS; ++i) 
	{
		scene_read[i] = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);

		const VkDescriptorImageInfo image = {sampler, scene_views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_read[i],
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &image,
		};
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}
}

void R_DestroyBloomHDR (void)
{
	for (int i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		if (scene_read[i])
			R_FreeDescriptorSet (scene_read[i], &vulkan_globals.single_texture_set_layout);
		scene_read[i] = VK_NULL_HANDLE;
	}

	for (int i = 0; i < BLOOMHDR_MAX_MIPS; ++i)
	{
		if (working_read[i])
			R_FreeDescriptorSet (working_read[i], &vulkan_globals.single_texture_set_layout);
		if (working_write[i])
			R_FreeDescriptorSet (working_write[i], &vulkan_globals.single_texture_cs_write_set_layout);
		vkDestroyImageView (vulkan_globals.device, working_views[i], NULL);
		vkDestroyImage (vulkan_globals.device, working_images[i], NULL);
		if (working_memory[i].handle)
			R_FreeVulkanMemory (&working_memory[i], &num_vulkan_misc_allocations);

		working_read[i] = VK_NULL_HANDLE;
		working_write[i] = VK_NULL_HANDLE;
		working_views[i] = VK_NULL_HANDLE;
		working_images[i] = VK_NULL_HANDLE;
	}

	if (delta_read)
		R_FreeDescriptorSet (delta_read, &vulkan_globals.single_texture_set_layout);
	if (delta_write)
		R_FreeDescriptorSet (delta_write, &vulkan_globals.single_texture_cs_write_set_layout);
	vkDestroyImageView (vulkan_globals.device, delta_view, NULL);
	vkDestroyImage (vulkan_globals.device, delta_image, NULL);
	if (delta_memory.handle)
		R_FreeVulkanMemory (&delta_memory, &num_vulkan_misc_allocations);
	delta_read = VK_NULL_HANDLE;
	delta_write = VK_NULL_HANDLE;
	delta_view = VK_NULL_HANDLE;
	delta_image = VK_NULL_HANDLE;

	vkDestroySampler (vulkan_globals.device, sampler, NULL);
	sampler = VK_NULL_HANDLE;

	effective_mips = 0;
}

void R_ComputeBloomHDR (cb_context_t *cbx)
{
	assert (r_bloomhdr.value > 0); // checked outside
	
	if (!r_scene_vrect.width || !r_scene_vrect.height)
		return;
	
	const VkCommandBuffer cb = cbx->cb;
	
	VkImageMemoryBarrier barriers[BLOOMHDR_MAX_MIPS + 1];
	for (int i = 0; i < effective_mips + 1; ++i)
	{
		barriers[i] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = i < effective_mips ? working_images[i] : delta_image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
	}
	vulkan_globals.vk_cmd_pipeline_barrier(
		cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, effective_mips + 1, barriers);
	
	const uint32_t scene_index = R_SceneColorBufferIndex (); // TODO: this is ugly!!!
	
	// Scene color
	VkImageMemoryBarrier scene_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = vulkan_globals.color_buffers[scene_index],
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &scene_barrier);

	const VkMemoryBarrier memory_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};

	#define BLOOM_PASS(pipeline, src_set, src_w, src_h, dst_set, dst_w, dst_h)                                                                          \
		do                                                                                                                                              \
		{                                                                                                                                               \
			vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, (pipeline).handle);                                                \
			const VkDescriptorSet sets[] = {(src_set), (dst_set), scene_read[scene_index]};                                                             \
			vulkan_globals.vk_cmd_bind_descriptor_sets (cb, VK_PIPELINE_BIND_POINT_COMPUTE, bloomhdr_compute_layout.handle, 0, 3, sets, 0, NULL);       \
			const bloomhdr_constants_t constants = {                                                                                                    \
				.src_size = {(float)(src_w), (float)(src_h)},                                                                                           \
				.inv_src_size = {1.f / (float)(src_w), 1.f / (float)(src_h)},                                                                           \
				.dst_size = {(float)(dst_w), (float)(dst_h)},                                                                                           \
				.inv_dst_size = {1.f / (float)(dst_w), 1.f / (float)(dst_h)},                                                                           \
				.intensity = r_bloomhdr_intensity.value * BLOOMHDR_INTENSITYFUDGE,                                                                      \
				.threshold = r_bloomhdr_threshold.value,                                                                                                \
			};                                                                                                                                          \
			vulkan_globals.vk_cmd_push_constants (cb, bloomhdr_compute_layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);  \
			vulkan_globals.vk_cmd_dispatch (cb, ((dst_w) + 7) / 8, ((dst_h) + 7) / 8, 1);                                                               \
			vulkan_globals.vk_cmd_pipeline_barrier (                                                                                                    \
				cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);               \
		} while (0)

	// DownFilter
	BLOOM_PASS (bloomhdr_compute_pipeline_downfilter, scene_read[scene_index], vid.render_width, vid.render_height, working_write[0], working_width[0], working_height[0]);

	// Down
	for (int i = 1; i < effective_mips; ++i)
		BLOOM_PASS (bloomhdr_compute_pipeline_down, working_read[i - 1], working_width[i - 1], working_height[i - 1], working_write[i], working_width[i], working_height[i]);

	// Up
	for (int i = effective_mips - 2; i >= 0; --i)
		BLOOM_PASS (bloomhdr_compute_pipeline_up, working_read[i + 1], working_width[i + 1], working_height[i + 1], working_write[i], working_width[i], working_height[i]);

	// UpFilter
	BLOOM_PASS (bloomhdr_compute_pipeline_upfilter, working_read[0], working_width[0], working_height[0], delta_write, vid.render_width, vid.render_height);
	#undef BLOOM_PASS

	// Scene color back to what the bloom render pass expects; the delta image is made visible to the composite by the render pass dependency.
	scene_barrier.srcAccessMask = 0;
	scene_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	scene_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	scene_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &scene_barrier);
}

void R_DrawBloomHDRTask (void *unused)
{
	(void)unused;

	assert (r_bloomhdr.value > 0);

	if (!r_scene_vrect.width || !r_scene_vrect.height)
		return;

	// TODO: Either this is stupid, or absolute big brain. composite utilizes blend for addition in gamma space.
	cb_context_t  *cbx = vulkan_globals.secondary_cb_contexts[SCBX_BLOOMHDR_COMPOSITE];
	const VkRect2D rect = {{r_scene_vrect.x, r_scene_vrect.y}, {r_scene_vrect.width, r_scene_vrect.height}};
	const VkViewport viewport = {rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0, 1};
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
	vkCmdSetScissor (cbx->cb, 0, 1, &rect);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomhdr_fragment_pipelines[cbx->pipeline_variant]);
	vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomhdr_fragment_layout.handle, 0, 1, &delta_read, 0, NULL);
	const bloomhdr_constants_t constants = {
		.src_size = {(float)working_width[0], (float)working_height[0]},
		.inv_src_size = {1.f / vid.render_width, 1.f / vid.render_height}, // shader maps gl_FragCoord to the bloom texture with this
	};
	R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_draw (cbx->cb, 3, 1, 0, 0);
}