#version 450
layout(set = 0, binding = 0) uniform Block { vec4 tint; vec4 params; } blk;
layout(set = 0, binding = 1, std430) readonly buffer Ssbo { vec4 v[]; } ssbo;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = (blk.tint * 0.6 + ssbo.v[0] * 0.4) * (0.7 + 0.3 * v_uv.x);
}
