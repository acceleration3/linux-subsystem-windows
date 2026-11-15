#version 450
layout(set = 0, binding = 0) uniform sampler2DArray u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 cell = floor(v_uv * 2.0);
    float layer = cell.x + cell.y * 2.0;
    vec2 local = fract(v_uv * 2.0);
    out_color = texture(u_tex, vec3(local, layer));
}
