R""(

// Default float/sampler precision, plus every declaration below is explicit
// highp too: on GPUs that actually enforce GLES precision (e.g. PowerVR),
// mediump/lowp are as low as 10/8 bits of mantissa, which is visibly not
// enough for texture coordinates (texture swimming/jitter) or the detail-map
// distance blend.
precision highp float;
precision highp sampler2D;

varying highp vec3 vPosition;
#ifdef SF_Texture0
varying highp vec2 vTexCoord0;
#endif
#ifdef SF_Texture1
varying highp vec2 vTexCoord1;
#endif
#ifdef SF_Texture2
varying highp vec2 vTexCoord2;
#endif
#ifdef SF_Texture3
varying highp vec2 vTexCoord3;
#endif
#ifdef SF_VtxColor
varying highp vec4 vVtxColor;
#endif
#ifdef SF_VtxFog
varying highp vec4 vVtxFog;
#endif

uniform highp float uBrightness;

#ifdef SF_Texture0
uniform highp sampler2D uTexture0;
#endif
#ifdef SF_Texture1
uniform highp sampler2D uTexture1;
#endif
#ifdef SF_Texture2
uniform highp sampler2D uTexture2;
#endif
#ifdef SF_Texture3
uniform highp sampler2D uTexture3;
#endif

#ifndef LIGHTMAP_OVERBRIGHT
#define LIGHTMAP_OVERBRIGHT 1.0
#endif

void main()
{
	highp vec4 Out = vec4(1.0);

#ifdef SF_Texture0
	highp vec4 Texel0 = texture2D(uTexture0, vTexCoord0);
	Out = Texel0;
#endif

#ifdef SF_AlphaTest
	if( Out.a < 0.5 )
		discard;
#endif

#ifdef SF_Texture1
	highp vec4 Texel1 = texture2D(uTexture1, vTexCoord1);
#ifdef SF_Lightmap
	Out.rgb *= Texel1.rgb * 2.0 * LIGHTMAP_OVERBRIGHT;
#else
	Out *= Texel1;
#endif
#endif

#ifdef SF_Texture2
	highp vec4 Texel2 = texture2D(uTexture2, vTexCoord2);
#ifdef SF_Fogmap
	Texel2 *= 2.0;
	Out.rgb = Texel2.rgb + Out.rgb * (1.0 - Texel2.a);
	Out.a = Texel2.a;
#else
	Out *= Texel2;
#endif
#endif

#ifdef SF_Texture3
	highp vec4 Texel3 = texture2D(uTexture3, vTexCoord3) * 2.0;
#ifdef SF_Detail
	// TODO: XOpenGLDrv does some RGB->HSV->RGB magic on detail maps, we just * 2 instead
	highp float DetailScale = clamp(0.65 - vPosition.z / 380.0, 0.0, 1.0);
	Texel3 = mix(vec4(1.0), Texel3, DetailScale);
	Out.rgb *= Texel3.rgb;
#else
	Out.rgb *= Texel3.rgb;
#endif
#endif

#ifdef SF_VtxColor
	Out *= vVtxColor;
#endif

#ifdef SF_VtxFog
	Out.rgb = vVtxFog.rgb + Out.rgb * (1.0 - vVtxFog.a);
#endif

	// Desktop (NOpenGLDrv.cpp) feeds this same "0.5 + 1.5*Brightness" value to
	// SDL_SetWindowBrightness(), which is a true gamma-ramp control (SDL
	// builds the ramp as pow(x, 1/gamma)) - not a linear scale. A linear
	// multiply here can never lift a nearly-black pixel into visible range
	// (this is what kept shadowed/unlit areas pitch black even at max
	// brightness), whereas the matching gamma curve brightens dark detail
	// much more than it does highlights, same as the desktop path.
	highp float Gamma = 0.5 + 1.5 * uBrightness;
	Out.rgb = pow( max( Out.rgb, 0.0 ), vec3( 1.0 / Gamma ) );

	gl_FragColor = Out;
}

)""
