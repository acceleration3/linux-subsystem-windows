#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput u_input;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 c = subpassLoad(u_input);
    out_color = vec4(c.bgr * vec3(1.0, 0.8, 1.2) + vec3(v_uv.x * 0.1), 1.0);
}
