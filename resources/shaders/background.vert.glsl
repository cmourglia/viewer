layout (location = 0) in vec3 in_position;

layout (location = 0) out vec3 out_position;

uniform mat4 proj;
uniform mat4 view;

void main()
{
    out_position = in_position;

    // Remove translation
    mat4 rotView = mat4(mat3(view));
    vec4 clipPos = proj * rotView * vec4(in_position, 1.0);

    gl_Position = clipPos.xyww;
}