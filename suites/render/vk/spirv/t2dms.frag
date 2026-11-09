#version 450
layout(set = 0, binding = 0) uniform sampler2DMS u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    ivec2 size = textureSize(u_tex);
    int sample_index = int(floor(v_uv.x * 4.0));
    ivec2 texel = ivec2(fract(v_uv.x * 4.0) * float(size.x), v_uv.y * float(size.y));
    out_color = texelFetch(u_tex, min(texel, size - 1), min(sample_index, 3));
}
