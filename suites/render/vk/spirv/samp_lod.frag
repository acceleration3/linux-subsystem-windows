#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform P { mat4 m; vec4 uv; vec4 params; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = textureLod(u_tex, v_uv, pc.params.y);
}
