layout (local_size_x = 32, local_size_y = 32) in;

layout (rgba32f, binding = 0) readonly restrict uniform image2D input_image;
layout (rgba32f, binding = 1) writeonly restrict uniform image2D output_image;

uniform float threshold;

vec3 convertRGB2XYZ(vec3 color)
{
	// Reference:
	// RGB/XYZ Matrices
	// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
	vec3 xyz;
	xyz.x = dot(vec3(0.4124564, 0.3575761, 0.1804375), color);
	xyz.y = dot(vec3(0.2126729, 0.7151522, 0.0721750), color);
	xyz.z = dot(vec3(0.0193339, 0.1191920, 0.9503041), color);
	return xyz;
}

vec3 convertXYZ2Yxy(vec3 color)
{
	// Reference:
	// http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
	float inv = 1.0/dot(color, vec3(1.0, 1.0, 1.0) );
	return vec3(color.y, color.x*inv, color.y*inv);
}

vec3 convertRGB2Yxy(vec3 color)
{
	return convertXYZ2Yxy(convertRGB2XYZ(color) );
}

vec3 convertXYZ2RGB(vec3 color)
{
	vec3 rgb;
	rgb.x = dot(vec3( 3.2404542, -1.5371385, -0.4985314), color);
	rgb.y = dot(vec3(-0.9692660,  1.8760108,  0.0415560), color);
	rgb.z = dot(vec3( 0.0556434, -0.2040259,  1.0572252), color);
	return rgb;
}

vec3 convertYxy2XYZ(vec3 color)
{
	// Reference:
	// http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
	vec3 xyz;
	xyz.x = color.x*color.y/color.z;
	xyz.y = color.x;
	xyz.z = color.x*(1.0 - color.y - color.z)/color.z;
	return xyz;
}

vec3 convertYxy2RGB(vec3 color)
{
	return convertXYZ2RGB(convertYxy2XYZ(color) );
}

#include "base_math.glsl"

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	ivec2 inTexel = texel * 2;

    vec4 row0 = mix(imageLoad(input_image, inTexel), imageLoad(input_image, inTexel + ivec2(1, 0)), 0.5);
	vec4 row1 = mix(imageLoad(input_image, inTexel + ivec2(0, 1)), imageLoad(input_image, inTexel + ivec2(1, 1)), 0.5);
	vec3 color = mix(row0, row1, 0.5).rgb;

    float luminance = convertRGB2Yxy(color).r;
	float bloomLuminance = luminance - threshold;
	float bloomAmount = saturate(bloomLuminance);

	imageStore(output_image, texel, vec4(color * bloomAmount, 1.0));
}