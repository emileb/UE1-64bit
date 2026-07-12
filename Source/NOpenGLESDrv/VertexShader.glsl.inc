R""(

// Everything highp: on GPUs that actually enforce GLES precision (e.g.
// PowerVR), mediump/lowp are as low as 10/8 bits of mantissa - nowhere near
// enough for world-space vertex positions or texture coordinates, and causes
// visible vertex jitter/swimming and texture-sample instability as the camera
// moves away from the map origin.
attribute highp vec3 aPosition;
varying highp vec3 vPosition;
#ifdef SF_Texture0
attribute highp vec2 aTexCoord0;
varying highp vec2 vTexCoord0;
#endif
#ifdef SF_Texture1
attribute highp vec2 aTexCoord1;
varying highp vec2 vTexCoord1;
#endif
#ifdef SF_Texture2
attribute highp vec2 aTexCoord2;
varying highp vec2 vTexCoord2;
#endif
#ifdef SF_Texture3
attribute highp vec2 aTexCoord3;
varying highp vec2 vTexCoord3;
#endif
#ifdef SF_VtxColor
attribute highp vec4 aVtxColor;
varying highp vec4 vVtxColor;
#endif
#ifdef SF_VtxFog
attribute highp vec4 aVtxFog;
varying highp vec4 vVtxFog;
#endif

uniform highp mat4 uMtx;

void main()
{
#ifdef SF_Texture0
	vTexCoord0 = aTexCoord0;
#endif
#ifdef SF_Texture1
	vTexCoord1 = aTexCoord1;
#endif
#ifdef SF_Texture2
	vTexCoord2 = aTexCoord2;
#endif
#ifdef SF_Texture3
	vTexCoord3 = aTexCoord3;
#endif
#ifdef SF_VtxColor
	vVtxColor = aVtxColor;
#endif
#ifdef SF_VtxFog
	vVtxFog = aVtxFog;
#endif
#ifdef SF_Detail
	vPosition = aPosition;
#endif
	gl_Position = uMtx * vec4(aPosition, 1.0);
}

)""
