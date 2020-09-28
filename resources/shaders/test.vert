#define POSITION_LOCATION  0
#define NORMAL_LOCATION    1
#define TANGENT_LOCATION   2
#define TEXCOORD0_LOCATION 3
#define TEXCOORD1_LOCATION 4
#define TEXCOORD2_LOCATION 5
#define TEXCOORD3_LOCATION 6
#define TEXCOORD4_LOCATION 7
#define COLOR_LOCATION     8
#define JOINTS_LOCATION    9
#define WEIGHTS_LOCATION   10
#define CUSTOM0_LOCATION   11
#define CUSTOM1_LOCATION   12
#define CUSTOM2_LOCATION   13
#define CUSTOM3_LOCATION   14

layout (location = POSITION_LOCATION) in vec3 in_position;
layout (location = NORMAL_LOCATION) in vec3 in_normal;
layout (location = TANGENT_LOCATION) in vec3 in_tangent;
layout (location = TEXCOORD0_LOCATION) in vec2 in_texcoord;

layout (location = 0) out vec3 out_position;
layout (location = 1) out vec3 out_normal;
layout (location = 2) out vec2 out_texcoord;

#ifdef HAS_NORMAL_MAP
#endif

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

void main()
{
    vec4 position = u_model * vec4(in_position, 1.0);
    out_position = position.xyz;
    out_normal = in_normal;
    // out_texcoord = vec2(in_texcoord.x, 1 - in_texcoord.y);
    out_texcoord = in_texcoord;

    gl_Position = u_proj * u_view * position;

#ifdef HAS_NORMAL_MAP
#endif
}