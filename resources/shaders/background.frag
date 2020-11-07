layout (location = 0) in vec3 in_position;

layout (location = 0) out vec4 out_color;

uniform samplerCube envmap;

void main()
{
    vec3 envColor = texture(envmap, in_position).rgb;

    envColor = envColor / (envColor + 1.0);
    envColor = pow(envColor, vec3(1.0 / 2.2));

    out_color = vec4(envColor, 1.0);
}