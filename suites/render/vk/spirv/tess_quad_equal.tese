#version 450
layout(quads, equal_spacing, cw) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec2 t = gl_TessCoord.xy;
    gl_Position = mix(mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x), mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x), t.y);
    v_color = mix(mix(t_color[0], t_color[1], t.x), mix(t_color[3], t_color[2], t.x), t.y);
}
