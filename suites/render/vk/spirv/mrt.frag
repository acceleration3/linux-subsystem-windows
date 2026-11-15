#version 450
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_aux;
void main() {
    out_color = vec4(v_color, 1.0);
    out_aux = vec4(v_color.bgr * 0.5 + 0.25, 0.75);
}
