layout (location = 0) in vec3 in_position;
layout (location = 0) out vec4 out_color;

uniform samplerCube envmap;
uniform int miplevel;

vec3 tonemap_Uchimura(vec3 x, float P, float a, float m, float l, float c, float b) {
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

vec3 tonemap_Uchimura(vec3 x) {
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black
    const float b = 0.0;  // pedestal
    return tonemap_Uchimura(x, P, a, m, l, c, b);
}

vec3 gamma(vec3 v) {
    vec3 r;

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

void main()
{
    vec3 color = textureLod(envmap, in_position, miplevel).rgb;

    // color = tonemap_Uchimura(color);
    // color = gamma(color);

    // color = vec3(0);
    out_color = vec4(color, 1.0);
}