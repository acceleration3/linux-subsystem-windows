#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
float radical(uint bits) {
    bits = bitfieldReverse(bits);
    return float(bits) * 2.3283064365386963e-10;
}
void main() {
    float ndv = max(v_uv.x, 0.02);
    float rough = max(v_uv.y, 0.04);
    vec3 v = vec3(sqrt(1.0 - ndv * ndv), 0.0, ndv);
    float a = rough * rough;
    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < 96u; ++i) {
        float u1 = (float(i) + 0.5) / 96.0;
        float u2 = radical(i);
        float phi = 6.2831853 * u1;
        float cos_theta = sqrt((1.0 - u2) / (1.0 + (a * a - 1.0) * u2));
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float ndl = max(l.z, 0.0);
        float ndh = max(h.z, 0.0);
        float vdh = max(dot(v, h), 0.0);
        if (ndl > 0.0) {
            float k = a * 0.5;
            float gv = ndv / (ndv * (1.0 - k) + k);
            float gl = ndl / (ndl * (1.0 - k) + k);
            float g_vis = gv * gl * vdh / (ndh * ndv);
            float fc = pow(1.0 - vdh, 5.0);
            A += (1.0 - fc) * g_vis;
            B += fc * g_vis;
        }
    }
    out_color = vec4(A / 96.0, B / 96.0, 0.0, 1.0);
}
