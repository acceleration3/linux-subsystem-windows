#version 450
layout(quads, fractional_even_spacing, cw) in;
layout(location = 0) in vec4 t_color[];
layout(location = 0) out vec4 v_color;
vec4 bern(float t) {
    float s = 1.0 - t;
    return vec4(s * s * s, 3.0 * t * s * s, 3.0 * t * t * s, t * t * t);
}
void main() {
    vec4 bu = bern(gl_TessCoord.x);
    vec4 bv = bern(gl_TessCoord.y);
    vec4 p = vec4(0.0);
    vec4 c = vec4(0.0);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            float w = bu[i] * bv[j];
            p += gl_in[j * 4 + i].gl_Position * w;
            c += t_color[16 + j * 4 + i] * w;
        }
    }
    gl_Position = p;
    v_color = c;
}
