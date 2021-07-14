layout (local_size_x = 32, local_size_y = 32) in;

uniform vec2 viewportSize;
uniform float bloomAmount;

layout (rgba8, binding = 0) writeonly restrict uniform image2D output_image;

layout (binding = 1) uniform sampler2D colorTexture;
layout (binding = 2) uniform sampler2D bloomTexture0;
layout (binding = 3) uniform sampler2D bloomTexture1;


vec3 Tonemap_Uchimura(vec3 x, float P, float a, float m, float l, float c, float b) {
    // Uchimura 2017, "HDR theory and practice"
    // Math: https://www.desmos.com/calculator/gslcdxvipg
    // Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 w0 = 1.0 - smoothstep(0.0, m, x);
    vec3 w2 = step(m + l0, x);
    vec3 w1 = 1.0 - w0 - w2;

    vec3 T = m * pow(x / m, vec3(c)) + b;
    vec3 S = P - (P - S1) * exp(CP * (x - S0));
    vec3 L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

vec3 Tonemap_Uchimura(vec3 x) {
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black
    const float b = 0.0;  // pedestal
    return Tonemap_Uchimura(x, P, a, m, l, c, b);
}

vec3 gamma_correct(vec3 v) {
    vec3 r;

    return pow(v, vec3(1.0 / 2.2));

    for (int i = 0; i < 3; ++i) {
        float f = v[i];
        if (f <= 0.0031308f) {
            r[i] = f * 12.92f;
        } else {
            r[i] = 1.055f * pow(f, 1.0f / 2.4f) - 0.055f;
        }
    }
    return r;
}

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

    const int windowSize = 15;

    vec3 color = texture(colorTexture, (texel + 0.5) / viewportSize).rgb;
    vec3 bloom = vec3(0.0);
    bloom = texture(bloomTexture0, (texel + 0.5) / viewportSize).rgb;
    // bloom = textureLod(bloomTexture1, texel / viewportSize, 2).rgb;

    vec3 finalColor = color + bloom * 0.2;

    // finalColor = Tonemap_Uchimura(finalColor);
    // finalColor = gamma_correct(finalColor);

    imageStore(output_image, texel, vec4(finalColor, 1.0));
    // imageStore(output_image, texel, vec4(color, 1.0));
    // imageStore(output_image, texel, vec4(bloom, 1.0));
}