#version 450
layout(push_constant) uniform P { mat4 vp; vec4 light; } pc;
layout(location = 0) in vec3 v_world;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(length(v_world - pc.light.xyz) / pc.light.w, 0.0, 0.0, 1.0);
}
