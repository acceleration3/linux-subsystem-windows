#version 450
layout(constant_id = 3) const int N = 4;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    float weights[N];
    float total = 0.0;
    for (int i = 0; i < N; ++i) {
        weights[i] = 1.0 / float(i + 1);
        total += weights[i];
    }
    float acc = 0.0;
    for (int i = 0; i < N; ++i) {
        acc += weights[i] * step(float(i) / float(N), v_uv.x);
    }
    out_color = vec4(acc / total, v_uv.y, 1.0 - acc / total, 1.0);
}
