#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform P { mat4 m; vec4 uv; vec4 params; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    int mode = int(pc.params.x + 0.5);
    vec4 c;
    if (mode == 0) {
        c = texture(u_tex, v_uv, pc.params.y);
    } else if (mode == 1) {
        c = textureLod(u_tex, v_uv, pc.params.y);
    } else if (mode == 2) {
        c = textureGrad(u_tex, v_uv, dFdx(v_uv) * pc.params.y, dFdy(v_uv) * pc.params.y);
    } else if (mode == 3) {
        c = textureGather(u_tex, v_uv, 0);
    } else if (mode == 4) {
        vec2 q = textureQueryLod(u_tex, v_uv);
        c = vec4(q.x / 6.0, q.y / 6.0, 0.5, 1.0);
    } else if (mode == 5) {
        c = textureGatherOffset(u_tex, v_uv, ivec2(1, -1), 1);
    } else if (mode == 7) {
        c = textureGather(u_tex, v_uv, 2);
    } else {
        c = textureOffset(u_tex, v_uv, ivec2(1, 1));
    }
    out_color = c;
}
