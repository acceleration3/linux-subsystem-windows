#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform sampler2D u_tex[];
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_mat;
layout(location = 2) in float v_light;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(texture(u_tex[nonuniformEXT(v_mat)], v_uv).rgb * v_light, 1.0);
}
