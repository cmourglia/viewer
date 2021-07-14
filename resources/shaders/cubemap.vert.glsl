layout (location = 0) in vec3 in_position;

layout (location = 0) out vec3 out_position;

uniform mat4 proj;
uniform mat4 view;

void main() {
    out_position = in_position;
    gl_Position = proj * view * vec4(in_position, 1.0);
}