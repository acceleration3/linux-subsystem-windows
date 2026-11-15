#version 450
layout(quads, equal_spacing, cw) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec2 t = gl_TessCoord.xy;
    vec4 p0 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x);
    vec4 p1 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x);
    vec4 c0 = mix(t_color[0], t_color[1], t.x);
    vec4 c1 = mix(t_color[3], t_color[2], t.x);
    gl_Position = mix(p0, p1, t.y);
    gl_Position.xy += 0.05 * vec2(sin(t.y * 9.0), sin(t.x * 9.0));
    v_color = mix(c0, c1, t.y);
}
