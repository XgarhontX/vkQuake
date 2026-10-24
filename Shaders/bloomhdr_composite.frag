#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushConsts
{
	vec2  src_size;
	vec2  inv_src_size;
	float p0;
	float p1;
	float p2;
	float final_pass;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D bloom_texture; // full resolution gamma-space delta

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	// inv_src_size is the reciprocal of the full render size here
	const vec2 uv = (gl_FragCoord.xy) * push_constants.inv_src_size;
	out_frag_color = vec4 (textureLod (bloom_texture, uv, 0.0).rgb, 0.0); // additively blended
}
