#version 450
layout(constant_id = 0) const int MODE = 1;
layout(constant_id = 1) const float SCALE = 3.0;
layout(constant_id = 2) const bool INVERT = true;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 c;
    if (MODE == 0) {
        c = vec3(v_uv.x, v_uv.y, 0.2);
    } else if (MODE == 1) {
        c = vec3(fract(v_uv.x * 8.0 * SCALE), 0.3, fract(v_uv.y * 8.0 * SCALE));
    } else {
        c = vec3(length(v_uv - vec2(0.5)) * SCALE, v_uv.x * 0.5, 0.6);
    }
    if (INVERT) {
        c = vec3(1.0) - c;
    }
    out_color = vec4(c, 1.0);
}
