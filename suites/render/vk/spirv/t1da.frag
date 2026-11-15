#version 450
layout(set = 0, binding = 0) uniform sampler1DArray u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    float layer = floor(v_uv.y * 4.0);
    out_color = texture(u_tex, vec2(v_uv.x, min(layer, 3.0)));
}
