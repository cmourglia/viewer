layout (location = 0) in vec3 in_position;

layout (location = 0) out vec4 out_color;

uniform samplerCube envMap;

const float PI = 3.14159265359;

void main()
{
    vec3 normal = normalize(in_position);

    vec3 irradiance = vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, normal);
    up = cross(normal, right);

    float sampleDelta = 0.25 * PI;
    // float sampleDelta = 0.5;
    float sampleCount = 0.0;

    // float phi = PI;
    // float theta = PI * 0.5;
    for (float phi = 0.0; phi < 2.0 * PI; phi += 0.025)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += 0.025)
        {
            float ctheta = cos(theta);
            float stheta = sin(theta);
            // Spherical to cartesian (in tangent space)
            vec3 tangentSample = vec3(stheta * cos(phi), stheta * sin(phi), ctheta);
            // Tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;

            irradiance += texture(envMap, sampleVec).rgb * ctheta * stheta;

            sampleCount += 1.0;
        }
    }


    irradiance = PI * irradiance * (1.0 / sampleCount);
    // irradiance = texture(envMap, normal).rgb;

    out_color = vec4(irradiance, 1.0);
}