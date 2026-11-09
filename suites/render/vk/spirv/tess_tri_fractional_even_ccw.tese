#version 450
layout(triangles, fractional_even_spacing, ccw) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec3 b = gl_TessCoord;
    gl_Position = gl_in[0].gl_Position * b.x + gl_in[1].gl_Position * b.y + gl_in[2].gl_Position * b.z;
    v_color = t_color[0] * b.x + t_color[1] * b.y + t_color[2] * b.z;
}
