#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	// float gamma;
	// float contrast;
	float shoulder;
	float toe;
	float peak;
	float paperscene;
	float hdr_enabled;
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
vec3 LinearTosRGB(vec3 linear) {
	return mix(
		linear * 12.92,
		1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
		greaterThan(linear, vec3(0.0031308))
	);
}

// Uchimura 2018, "Practical HDR and Wide Color Techniques in Gran Turismo SPORT"
// https://www.desmos.com/calculator/gslcdxvipg
// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
vec3 GTTonemap(vec3 x, float p, float m, float c) 
{
	const float a = 1.0; // will unroll, surely
	const float l = 0.0;
	const float b = 0.0;

	const float l0 = ((p - m) * l) / a;
	const float S0 = m + l0;
	const float S1 = m + a * l0;
	const float C2 = (a * p) / (p - S1);
	const float CP = -C2 / p;

	const vec3 w0 = vec3(1.0) - smoothstep(vec3(0.0), vec3(m), x);
	const vec3 w2 = step(vec3(m + l0), x);
	const vec3 w1 = vec3(1.0) - w0 - w2;

	const vec3 T_ = vec3(m) * pow(vec3(x / m), vec3(c)) + vec3(b);
	const vec3 S_ = vec3(p) - (vec3(p) - vec3(S1)) * exp(vec3(CP) * (x - vec3(S0)));
	const vec3 L_ = vec3(m) + vec3(a) * (x - vec3(m));

	return T_ * w0 + L_ * w1 + S_ * w2;
}

void main ()
{
	// vec3 frag = subpassLoad (color_input).rgb;
	// frag.rgb = frag.rgb * push_constants.contrast;
	// out_frag_color = vec4 (pow (frag, vec3 (push_constants.gamma)), 1.0);

	// load
	vec3 frag = subpassLoad (color_input).rgb;
	frag = max(frag, vec3(0.0)); // clean

	// gamma
	if (push_constants.hdr_enabled > 0.0)
	{
		// gamma correct upwards, then linearize
		frag = pow(frag, vec3(1/2.2)); 
		frag = sRGBToLinear(frag); 
		frag = pow(frag, vec3(2.2));
	} else {
		// linearize sRGB, which SDR mismatch will lower
		frag = sRGBToLinear(frag);
	}

	// exposure makeup for goofy contrast curves
	frag *= vec3(4.0); 
	
	// Tonemap
	if (push_constants.peak > 0) 
	{
		float p = (push_constants.peak / push_constants.paperscene);
		float s = (push_constants.shoulder * p);
		float t = (push_constants.toe);

		// GT Tonemap (Exponential Rolloff w/ Toe)
		frag = GTTonemap(frag, p, s, t);
	}

	frag *= vec3(push_constants.paperscene / 80.); // scRGB

	out_frag_color = vec4 (frag, 1.0);
}
