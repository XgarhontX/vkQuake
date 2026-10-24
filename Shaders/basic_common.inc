vec4 BasicFragmentColor ()
{
	vec4 frag_color = in_color * texture (tex, in_texcoord.xy);

	float fog = exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp (fog, 0.0, 1.0);
	frag_color.rgb = mix (push_constants.fog_color, frag_color.rgb, fog);

	frag_color = max(frag_color, vec4(0.0));
	frag_color.w = min (frag_color.w, 1.0);

	return frag_color;
}
