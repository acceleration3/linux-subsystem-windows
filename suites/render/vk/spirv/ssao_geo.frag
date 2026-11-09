#version 450
layout(location = 0) in vec3 v_n;
layout(location = 1) in vec3 v_c;
layout(location = 2) in float v_depth;
layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
void main() {
    out_albedo = vec4(v_c, 1.0);
    out_normal = vec4(normalize(v_n), v_depth);
}
