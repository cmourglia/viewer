layout (local_size_x = 32, local_size_y = 32) in;

layout (rgba32f, binding = 0) readonly restrict uniform image2D input_image;
layout (rgba32f, binding = 1) writeonly restrict uniform image2D output_image;

const int width = 9;
const int offsets[width] = int[](-4, -3, -2, -1, 0, 1, 2, 3, 4);
const float weights[width] = float[](0.01621622, 0.05405405, 0.12162162, 0.19459459, 0.22702703, 0.19459459, 0.12162162, 0.05405405, 0.01621622);

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID);

    vec3 color = vec3(0.0);
    float acc = 0.0;

    for (int i = 0; i < width; ++i)
    {
#if defined(HORIZONTAL_BLUR)
        // We need to downsample
        ivec2 inTexel = (texel + ivec2(offsets[i], 0)) * 2;
        vec3 row0 = mix(imageLoad(input_image, inTexel + ivec2(0, 0)).rgb, imageLoad(input_image, inTexel + ivec2(1, 0)).rgb, 0.5);
        vec3 row1 = mix(imageLoad(input_image, inTexel + ivec2(0, 1)).rgb, imageLoad(input_image, inTexel + ivec2(1, 1)).rgb, 0.5);
        vec3 down = mix(row0, row1, 0.5);

        // Then blur
        color += down * weights[i];
#elif defined(VERTICAL_BLUR)
        // We only need to blur
        color += imageLoad(input_image, texel + ivec2(0, offsets[i])).rgb * weights[i];
#endif
    }

    imageStore(output_image, texel, vec4(color, 1.0));
}
