#version 450
layout(isolines, equal_spacing) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    float angle = 6.2831853 * gl_TessCoord.x;
    float radius = 0.25 + 0.5 * gl_TessCoord.y;
    gl_Position = gl_in[0].gl_Position + vec4(cos(angle) * radius, sin(angle) * radius, 0.0, 0.0);
    v_color = t_color[0] * (0.5 + 0.5 * gl_TessCoord.y);
}
