#version 450
layout(quads, equal_spacing, ccw) in;
layout(set = 0, binding = 0) uniform sampler2D u_height;
layout(push_constant) uniform P { mat4 vp; vec4 params; } pc;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
void main() {
    vec2 t = gl_TessCoord.xy;
    vec4 p = mix(mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x), mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x), t.y);
    float h = textureLod(u_height, p.xy * 0.5 + 0.5, 0.0).r;
    gl_Position = pc.vp * vec4(p.x, h * pc.params.x, p.y, 1.0);
    v_color = vec4(mix(vec3(0.15, 0.35, 0.12), vec3(0.9, 0.85, 0.7), h), 1.0);
}
