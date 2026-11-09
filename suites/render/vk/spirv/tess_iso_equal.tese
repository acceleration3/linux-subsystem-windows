#version 450
layout(isolines, equal_spacing) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec2 t = gl_TessCoord.xy;
    vec4 p = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x);
    p.y += (t.y - 0.5) * 0.7;
    gl_Position = p;
    v_color = mix(t_color[0], t_color[1], t.x) * (0.4 + 0.6 * t.y);
}
