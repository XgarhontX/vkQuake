#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	vec3  eye_pos;
	float wind_phase;
	vec3  wind_dir;
	float skycubeboost;
}
push_constants;

layout (set = 0, binding = 0) uniform samplerCube tex;

layout (location = 0) in vec4 in_texcoord;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	// base color:
	out_frag_color = texture (tex, in_texcoord.xyz);

	if (push_constants.skycubeboost >= 1)
	{
		out_frag_color.rgb = pow (out_frag_color.rgb, vec3 (2.2));
		float y = dot (out_frag_color.rgb, vec3 (0.2126, 0.7152, 0.0722)); // BT709
		if (y > 0) 
		{
			// Neutwo Inv. f_{pi}\left(x\right)=\frac{px}{\sqrt{-xx+pp}}
			float p = push_constants.skycubeboost;
			float y1 = y;
			y1 = min (y1, p * 0.98); // safe
			y1 = (p * y1) / sqrt (-y1 * y1 + p * p);
			out_frag_color.rgb *= y1 / y;
		}
		out_frag_color.rgb = pow (out_frag_color.rgb, vec3(1/2.2));
	}

	// add wind:
	if (push_constants.wind_dir != vec3 (0.0f))
	{
		float t1 = push_constants.wind_phase;
		float t2 = fract (t1) - 0.5;
		float blend = abs (t1 * 2.0);
		vec3  dir = normalize (in_texcoord.xyz);
		vec4  layer1 = texture (tex, dir + t1 * push_constants.wind_dir);
		vec4  layer2 = texture (tex, dir + t2 * push_constants.wind_dir);
		layer1.a *= 1.0 - blend;
		layer2.a *= blend;
		layer1.rgb *= layer1.a;
		layer2.rgb *= layer2.a;
		vec4 combined = layer1 + layer2;

		// result:
		out_frag_color = vec4 (out_frag_color.rgb * (1.0 - combined.a) + combined.rgb, 1);
	}

	if (push_constants.fog_density > 0.0f)
		out_frag_color.rgb = (out_frag_color.rgb * (1.0f - push_constants.fog_density)) + (push_constants.fog_color * push_constants.fog_density);
	
	out_frag_color = max (out_frag_color, vec4(0.0));
	out_frag_color.w = min (out_frag_color.w, 1.0);
}
