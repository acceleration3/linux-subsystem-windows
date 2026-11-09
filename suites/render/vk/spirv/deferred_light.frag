#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput u_albedo;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput u_normal;
layout(push_constant) uniform P { vec4 dir[4]; vec4 col[4]; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 albedo = subpassLoad(u_albedo);
    vec3 n = normalize(subpassLoad(u_normal).xyz * 2.0 - 1.0);
    vec3 lit = vec3(0.08);
    for (int i = 0; i < 4; ++i) {
        lit += pc.col[i].rgb * max(dot(n, normalize(pc.dir[i].xyz)), 0.0);
    }
    float covered = albedo.a;
    out_color = vec4(albedo.rgb * lit * covered, 0.65);
}
