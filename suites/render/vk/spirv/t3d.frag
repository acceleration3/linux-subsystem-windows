#version 450
layout(set = 0, binding = 0) uniform sampler3D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    float col = floor(v_uv.x * 4.0);
    vec2 local = vec2(fract(v_uv.x * 4.0), v_uv.y);
    out_color = texture(u_tex, vec3(local, (col + 0.5) / 4.0));
}
