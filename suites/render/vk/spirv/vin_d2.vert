#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in dvec2 in_attr;
layout(location = 0) out vec4 v_color;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 12.0;
    dvec2 k = (in_attr - dvec2(0.5lf)) * 1099511627776.0lf;
    v_color = vec4(float(k.x) / 15.0, float(k.y) / 15.0, 0.0, 1.0);
}
