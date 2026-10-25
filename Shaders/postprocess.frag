#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	// float gamma;
	// float contrast;
	float shoulder;
	float peak;
	float paperscene;
	float hdr_gamma;
}
push_constants;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (location = 0) out vec4 out_frag_color;

vec3 sRGBToLinear(vec3 srgb) {
    return mix(
        srgb / 12.92, 
        pow((srgb + 0.055) / 1.055, vec3(2.4)), 
        greaterThan(srgb, vec3(0.04045))
    );
}

void main ()
{
	// vec3 frag = subpassLoad (color_input).rgb;
	// frag.rgb = frag.rgb * push_constants.contrast;
	// out_frag_color = vec4 (pow (frag, vec3 (push_constants.gamma)), 1.0);

	vec3 frag = subpassLoad (color_input).rgb;
	
	frag = max(frag, vec3(0.0)); // clean

	if (push_constants.hdr_gamma > 0.5) frag = pow(frag, vec3(2.2));
	else frag = sRGBToLinear(frag);
	frag *= vec3(4.0);// exposure makeup
	
	// Exponential Rolloff x+\left((p-s)*(1.0-\exp(-\max(0,x-s)/(p-s)))+(-\max(0,x-s))\right)
	if (push_constants.peak > 0) 
	{
		vec3 p = vec3(push_constants.peak / push_constants.paperscene);
		vec3 s = vec3(push_constants.shoulder * p);
		frag = frag + ((p - s) * (1.0 - exp(-max(vec3(0.0), frag - s) / (p - s))) + (-max(vec3(0.0), frag - s)));
	}

	frag *= vec3(push_constants.paperscene / 80.); // scRGB

	out_frag_color = vec4 (frag, 1.0);
}
