#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform sampler2D u_tex[];
layout(push_constant) uniform P { vec4 count; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    int index = int(floor(v_uv.x * 4.0)) + 4 * int(floor(v_uv.y * 2.0));
    if (index >= int(pc.count.x)) {
        out_color = vec4(0.2, 0.2, 0.2, 1.0);
        return;
    }
    out_color = texture(u_tex[nonuniformEXT(index)], fract(v_uv * vec2(4.0, 2.0)));
}
