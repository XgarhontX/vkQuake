// https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
// 13-tap downsample CoD AW

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout (push_constant) uniform PushConsts
{
	vec2  src_size;
	vec2  inv_src_size;
	vec2  dst_size;
	vec2  inv_dst_size;
	float intensity;
	float threshold;
	float p0;
	float p1;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D src_texture;
layout (set = 1, binding = 0, rgba16f) uniform writeonly image2D dst_image;


#ifdef BLOOMHDR_PREFILTER
	const float GAMMA = 2.2;
	const float EXPOSURE = 4.0;
	#define THRESHOLD push_constants.threshold
	#define KNEE (THRESHOLD / 2.f)

	vec3 Fetch (vec2 uv)
	{
		return pow (max (textureLod (src_texture, uv, 0.0).rgb, vec3 (0.0)), vec3 (GAMMA)) * EXPOSURE; // to linear & exposure compensate
	}

	float KarisWeight (vec3 c)
	{
		return 1.0 / (1.0 + dot (c, vec3 (0.2126, 0.7152, 0.0722))); // y weight
	}

	vec3 KarisAverage (vec3 a, vec3 b, vec3 c, vec3 d) // 4 tap weighted avg
	{
		float wa = KarisWeight (a);
		float wb = KarisWeight (b);
		float wc = KarisWeight (c);
		float wd = KarisWeight (d);
		return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
	}

	// Luma Bloom.hlsl
	vec3 QuadraticThreshold (vec3 c)
	{
		float br = max (c.r, max (c.g, c.b));
		br = max (br, 1e-6);

		const float k = max (1e-6, KNEE);
		const vec3 curve = vec3 (THRESHOLD - k, k * 2.0, 0.25 / k);
		float rq = clamp (br - curve.x, 0.0, curve.y);
		rq = curve.z * rq * rq;

		return c * max (rq, br - THRESHOLD) / br;
	}

	// Luma Color.hlsl
	vec3 SetChrominance(vec3 color, float chrominance)
	{
			float maxVal = max (max (color.r, color.g), color.b);
			float minVal = min (min (color.r, color.g), color.b);
			float midVal = mix (minVal, maxVal, 0.5);
			return mix (vec3(midVal), color, vec3(chrominance));
	}
#else
	vec3 Fetch (vec2 uv)
	{
		return textureLod (src_texture, uv, 0.0).rgb; // already linear
	}
#endif

void main ()
{
	const ivec2 id = ivec2 (gl_GlobalInvocationID.xy);
	if (id.x >= push_constants.dst_size.x || id.y >= push_constants.dst_size.y)
		return;

	vec3 result = vec3( 0.0f, 0.0f, 0.0f );
	const vec2 uv = (vec2 (id) + 0.5) * push_constants.inv_dst_size;

#if 0 // MODE: Karis
		const vec2 t = push_constants.inv_src_size;
		
		const vec3 a = Fetch (uv + t * vec2 (-2, 2));
		const vec3 b = Fetch (uv + t * vec2 (0, 2));
		const vec3 c = Fetch (uv + t * vec2 (2, 2));
		const vec3 d = Fetch (uv + t * vec2 (-2, 0));
		const vec3 e = Fetch (uv);
		const vec3 f = Fetch (uv + t * vec2 (2, 0));
		const vec3 g = Fetch (uv + t * vec2 (-2, -2));
		const vec3 h = Fetch (uv + t * vec2 (0, -2));
		const vec3 i = Fetch (uv + t * vec2 (2, -2));
		const vec3 j = Fetch (uv + t * vec2 (-1, 1));
		const vec3 k = Fetch (uv + t * vec2 (1, 1));
		const vec3 l = Fetch (uv + t * vec2 (-1, -1));
		const vec3 m = Fetch (uv + t * vec2 (1, -1));

	#ifdef BLOOMHDR_PREFILTER
		result = KarisAverage (j, k, l, m) * 0.5 + (KarisAverage (a, b, d, e) + KarisAverage (b, c, e, f) + KarisAverage (d, e, g, h) + KarisAverage (e, f, h, i)) * 0.125; // karis
	#else
		result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125; // normal
	#endif
#else // MODE: Froyok
		const vec2 Coords[13] = {
			vec2( -1.0f,  1.0f ), vec2(  1.0f,  1.0f ),
			vec2( -1.0f, -1.0f ), vec2(  1.0f, -1.0f ),

			vec2(-2.0f, 2.0f), vec2( 0.0f, 2.0f), vec2( 2.0f, 2.0f),
			vec2(-2.0f, 0.0f), vec2( 0.0f, 0.0f), vec2( 2.0f, 0.0f),
			vec2(-2.0f,-2.0f), vec2( 0.0f,-2.0f), vec2( 2.0f,-2.0f)
		};

		const float Weights[13] = {
			// 4 samples
			// (1 / 4) * 0.5f = 0.125f
			0.125f, 0.125f,
			0.125f, 0.125f,
			
			// 9 samples
			// (1 / 9) * 0.5f
			0.0555555f, 0.0555555f, 0.0555555f,
			0.0555555f, 0.0555555f, 0.0555555f,
			0.0555555f, 0.0555555f, 0.0555555f
		};

		for( int i = 0; i < 13; i++ )
		{
			vec2 uv1 = uv + Coords[i] * push_constants.inv_src_size;
			result += Weights[i] * Fetch (uv1);
		}
#endif

	#ifdef BLOOMHDR_PREFILTER
		result = SetChrominance (result, 1.2); // boost keeps bloom "bleaching" highlights
		result = max (result, vec3 (0.0)); // sat boost safe
		result = QuadraticThreshold (result);
	#endif

	imageStore (dst_image, id, vec4 (result, 1.0));
}
