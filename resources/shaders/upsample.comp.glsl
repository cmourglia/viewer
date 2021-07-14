layout (local_size_x = 32, local_size_y = 32) in;

layout (rgba32f, binding = 0) readonly restrict uniform image2D lowResImage;
layout (rgba32f, binding = 1) readonly restrict uniform image2D highResImage;
layout (rgba32f, binding = 2) writeonly restrict uniform image2D outImage;

const float boxFilter[3][3] = float[][](
    float[](1 / 16.0, 2 / 16.0, 1 / 16.0),
    float[](2 / 16.0, 4 / 16.0, 2 / 16.0),
    float[](1 / 16.0, 2 / 16.0, 1 / 16.0)
);

void main()
{
    ivec2 inTexel = ivec2(gl_GlobalInvocationID);
    ivec2 outTexel = inTexel * 2;

    vec3 c0 = imageLoad(lowResImage, inTexel + ivec2(0, 0)).rgb;
    vec3 c1 = imageLoad(lowResImage, inTexel + ivec2(1, 0)).rgb;
    vec3 c2 = imageLoad(lowResImage, inTexel + ivec2(0, 1)).rgb;
    vec3 c3 = imageLoad(lowResImage, inTexel + ivec2(1, 1)).rgb;

    vec3 newColor0 = mix(c0, c1, 0.5);
    vec3 newColor1 = mix(c0, c2, 0.5);
    vec3 tmp       = mix(c2, c3, 0.5);
    vec3 newColor2 = mix(newColor0, tmp, 0.5);

    c1 = newColor0;
    c2 = newColor1;
    c3 = newColor2;

    vec3 p0 = imageLoad(highResImage, outTexel + ivec2(0, 0)).rgb;
    vec3 p1 = imageLoad(highResImage, outTexel + ivec2(1, 0)).rgb;
    vec3 p2 = imageLoad(highResImage, outTexel + ivec2(0, 1)).rgb;
    vec3 p3 = imageLoad(highResImage, outTexel + ivec2(1, 1)).rgb;

    imageStore(outImage, outTexel + ivec2(0, 0), vec4(p0 + c0, 1.0));
    imageStore(outImage, outTexel + ivec2(1, 0), vec4(p1 + c1, 1.0));
    imageStore(outImage, outTexel + ivec2(0, 1), vec4(p2 + c2, 1.0));
    imageStore(outImage, outTexel + ivec2(1, 1), vec4(p3 + c3, 1.0));
}
