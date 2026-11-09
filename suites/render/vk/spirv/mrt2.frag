#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    o0 = v_color;
    o1 = vec4(1.0 - v_color.rgb, 1.0);
}
