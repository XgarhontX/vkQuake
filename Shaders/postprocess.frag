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
}
push_constants;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (location = 0) out vec4 out_frag_color;

//////////////////////////////////////////////////////////////////////////////

// const float epsilon = 1e-6f;
// 
// const float pi = 3.14159265358979324f;
// const float twoPi = 6.28318530717958648f;
// const float invTwoPi = 0.15915494309189534f;
// 
// // jzazbz numbers
// // will need to rederive the numbers if you want to use a different ucs
// const float bbPurpleHue = 0.19199f; //  11.0 deg  repeller (494c)
// const float bbYellowHue = 1.86750f; // 107.0 deg  ATTRACTOR (571nm)
// const float bbGreenHue = 2.95833f;  // 169.5 deg  repeller (506nm)
// const float bbBlueHue = 4.45932f; // 255.5 deg  ATTRACTOR (474nm)
// 
// const float bbArcToYellow = bbYellowHue - bbPurpleHue;
// const float bbArcToGreen = bbGreenHue - bbPurpleHue;
// const float bbArcToBlue = bbBlueHue - bbPurpleHue;
// 
// const float jzazbzExponentScale = 1.7f;
// const float jzazbzEpsilon = 1.6295499532821566e-11f;
// 
// const float toneAnchor = 0.18f;
// const float toneCompression = 1.5f;
// const float overshootShoulder = 0.8f;
// 
// const mat3 BT709_TO_BT2020_MAT = mat3(
//     vec3(0.6274039149284363, 0.06909728795289993, 0.0163914393633604),
//     vec3(0.3292830288410187, 0.9195404052734375, 0.08801330626010895),
//     vec3(0.04331306740641594, 0.01136231515556574, 0.8955952525138855));
// const mat3 BT2020_TO_BT709_MAT = mat3(
//     vec3(1.6604909896850586, -0.12455047667026520, -0.01815076358616352),
//     vec3(-0.5876411199569702, 1.1328998804092407, -0.10057889670133591),
//     vec3(-0.07284986227750778, -0.00834942236542702, 1.1187297105789185));
// vec3 BT709ToBT2020(vec3 bt709) { return BT709_TO_BT2020_MAT * bt709; }
// vec3 BT2020ToBT709(vec3 bt2020) { return BT2020_TO_BT709_MAT * bt2020; }
// 
// const vec3 M1 = vec3(2610.f / 16384.f);           // 0.1593017578125f;
// const vec3 M2 = vec3(128.f * (2523.f / 4096.f));  // 78.84375f;
// const vec3 C1 = vec3(3424.f / 4096.f);            // 0.8359375f;
// const vec3 C2 = vec3(32.f * (2413.f / 4096.f));   // 18.8515625f;
// const vec3 C3 = vec3(32.f * (2392.f / 4096.f));   // 18.6875f;
// 
// vec3 PQEncode(vec3 color, float scaling) {
//   color *= (scaling / 10000.f);
//   vec3 y_m1 = pow(color, M1);
//   return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
// }
// vec3 PQEncode(vec3 color) { 
//   return PQEncode(color, 10000.f);
// }
// 
// vec3 PQDecode(vec3 color, float scaling) {
//   vec3 e_m12 = pow(color, 1.f / M2);
//   vec3 out_color = pow(max(vec3(0), e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
//   return out_color * vec3(10000.f / scaling);
// }
// vec3 PQDecode(vec3 color) { 
//   return PQDecode(color, 10000.f);
// }
// 
// vec3 SafePow(vec3 color, float exponent) {
//   vec3 signColor = sign(color);
//   return signColor * pow(abs(color), vec3(exponent));
// }
// 
// float DivideSafe(float numerator, float denominator, float fallback) {
// 	return denominator != 0.f ? numerator / denominator : fallback;
// }
// vec3 DivideSafe(vec3 numerator, vec3 denominator, float fallback) 
// {
// 	return vec3(
// 		DivideSafe(numerator.x, denominator.x, fallback),
// 		DivideSafe(numerator.y, denominator.y, fallback),
// 		DivideSafe(numerator.z, denominator.z, fallback)
// 	);
// }
// 
// vec3 JzAzBzFromBT709(vec3 bt709, float referenceLuminance, float exponentScaleFactor) {
//   vec3 rgb = BT709ToBT2020(bt709);
// 
//   vec3 lms;
//   lms.x = rgb.x * 0.530004f + rgb.y * 0.355704f + rgb.z * 0.086090f;
//   lms.y = rgb.x * 0.289388f + rgb.y * 0.525395f + rgb.z * 0.157481f;
//   lms.z = rgb.x * 0.091098f + rgb.y * 0.147588f + rgb.z * 0.734234f;
// 
//   vec3 lmsPq = SafePow(clamp(PQEncode(max(lms, 0), referenceLuminance), 0.f, 1.f), exponentScaleFactor);
// 
//   float iz = 0.5f * lmsPq.x + 0.5f * lmsPq.y;
//   float jz = (0.44f * iz) / max(1.f - 0.56f * iz, epsilon) - jzazbzEpsilon;
//   float az = 3.524000f * lmsPq.x - 4.066708f * lmsPq.y + 0.542708f * lmsPq.z;
//   float bz = 0.199076f * lmsPq.x + 1.096799f * lmsPq.y - 1.295875f * lmsPq.z;
// 
//   return vec3(jz, az, bz);
// }
// 
// vec3 BT709FromJzAzBz(vec3 jzazbz, float referenceLuminance, float exponentScaleFactor) {
//   float jz = jzazbz.x + jzazbzEpsilon;
//   float iz = jz / max(0.44f + 0.56f * jz, epsilon);
//   float az = jzazbz.y;
//   float bz = jzazbz.z;
// 
//   float lPq = iz + az * 0.1386050432715393f + bz * 0.05804731615611869f;
//   float mPq = iz - az * 0.1386050432715393f - bz * 0.05804731615611869f;
//   float sPq = iz - az * 0.09601924202631895f - bz * 0.8118918960560390f;
// 
//   float inverseExponentScale = 1.f / max(exponentScaleFactor, epsilon);
// 
//   vec3 lmsPq = SafePow(clamp(vec3(lPq, mPq, sPq), -1.f, 1.f), inverseExponentScale);
//   vec3 lms = sign(lmsPq) * PQDecode(abs(lmsPq), referenceLuminance);
// 
//   vec3 bt2020;
//   bt2020.r = lms.x * 2.990669f - lms.y * 2.049742f + lms.z * 0.088977f;
//   bt2020.g = -lms.x * 1.634525f + lms.y * 3.145627f - lms.z * 0.483037f;
//   bt2020.b = -lms.x * 0.042505f - lms.y * 0.377983f + lms.z * 1.448019f;
// 
//   return BT2020ToBT709(bt2020);
// }
// 
// vec3 BT709FromJzAzBz(vec3 jzazbz) {
//   return BT709FromJzAzBz(jzazbz, 203.f, jzazbzExponentScale);
// }
// 
// // TheGreatHmmmmm
// float bezoldBruckeShift(float hue, float amount) {
//   float h = hue - bbPurpleHue;
//   h -= twoPi * floor(h * invTwoPi);
// 
//   float arcStart, arcEnd, arcSign;
//   if (h < bbArcToYellow) {  // purple -> yellow (reds, oranges) attractor at the far end
//     arcStart = 0.f;
//     arcEnd = bbArcToYellow;
//     arcSign = 1.f;
//   } else if (h < bbArcToGreen) {  // yellow -> green (yellow-greens) attractor at the near end
//     arcStart = bbArcToYellow;
//     arcEnd = bbArcToGreen;
//     arcSign = -1.f;
//   } else if (h < bbArcToBlue) {  // green -> blue greens, cyans) attractor at the far end
//     arcStart = bbArcToGreen;
//     arcEnd = bbArcToBlue;
//     arcSign = 1.f;
//   } else {  // blue -> purple (violets, magentas) attractor at the near end
//     arcStart = bbArcToBlue;
//     arcEnd = twoPi;
//     arcSign = -1.f;
//   }
// 
//   float arcLength = arcEnd - arcStart;
//   float t = (h - arcStart) / arcLength;
//   float distanceToAttractor = arcSign > 0.f ? (1.f - t) : t;
// 
//   return arcSign * arcLength * min(0.5f * sin(pi * t) * amount, distanceToAttractor);
// }
// vec3 hueShiftBezoldBrucke(vec3 jzazbz, float amount) {
// 	if (amount < epsilon) return jzazbz;
// 
//   float chroma = length(jzazbz.yz);
//   if (chroma < epsilon) return jzazbz;
// 
//   float hue = atan(jzazbz.z, jzazbz.y);
//   hue += bezoldBruckeShift(hue, clamp(amount, 0.f, 1.f));
// 
// 	float sinHue = sin(hue);
// 	float cosHue = cos(hue);
//   jzazbz.yz = vec2(cosHue, sinHue) * chroma;
// 
//   return jzazbz;
// }

//////////////////////////////////////////////////////////////////////////////

// Musa
const mat3 BT709_TO_BT2020_EXPANDED_BT709_MAT = mat3(
    vec3(0.9773816772f, 0.0112560072f, 0.0113623156f),
    vec3(0.0060849375f, 0.9825527470f, 0.0113623156f),
    vec3(0.0060849375f, 0.0112560072f, 0.9826590553f));
const mat3 BT2020_EXPANDED_BT709_TO_BT709_MAT = mat3(
    vec3(1.0232867278f, -0.0115886389f, -0.0116980889f),
    vec3(-0.0062647564f, 1.0179628453f, -0.0116980889f),
    vec3(-0.0062647564f, -0.0115886389f, 1.0178533953f));

//////////////////////////////////////////////////////////////////////////////

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
	frag = pow(frag, vec3(2.2));

	// exposure makeup for goofy contrast curves
	frag *= vec3(4.0); 
	
	// Tonemap
	float y = dot(frag, vec3(0.2126, 0.7152, 0.0722));
	if (push_constants.peak > 0) 
	{
		float p = (push_constants.peak / push_constants.paperscene);
		float s = (push_constants.shoulder * p);
		float t = (push_constants.toe);

		// GT Tonemap (Exponential Rolloff w/ Toe)
		frag = BT709_TO_BT2020_EXPANDED_BT709_MAT * frag;
		frag = GTTonemap(frag, p, s, t);
		frag = BT2020_EXPANDED_BT709_TO_BT709_MAT * frag;

		// frag = JzAzBzFromBT709(frag, 10000/203.f, 1.7);
		// frag = hueShiftBezoldBrucke(frag, y * push_constants.hue_shift);
		// frag = BT709FromJzAzBz(frag, 10000/203.f, 1.7);
		// frag = max(frag, vec3(0.0));
		// frag = min(frag, vec3(p));
	}

	frag *= vec3(push_constants.paperscene / 80.); // scRGB

	out_frag_color = vec4 (frag, 1.0);
}
