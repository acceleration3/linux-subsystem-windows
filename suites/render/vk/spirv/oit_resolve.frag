#version 450
layout(set = 0, binding = 0) readonly buffer Head { uint head[]; };
layout(set = 0, binding = 1) readonly buffer Nodes { uvec4 nodes[]; };
layout(push_constant) uniform P { vec4 params; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    uint pix = uint(gl_FragCoord.y) * uint(pc.params.x) + uint(gl_FragCoord.x);
    uint colors[12];
    float depths[12];
    int n = 0;
    uint idx = head[pix];
    while (idx != 0xFFFFFFFFu && n < 12) {
        uvec4 node = nodes[idx];
        colors[n] = node.x;
        depths[n] = uintBitsToFloat(node.y);
        ++n;
        idx = node.z;
    }
    for (int i = 1; i < n; ++i) {
        uint c = colors[i];
        float d = depths[i];
        int j = i - 1;
        while (j >= 0 && depths[j] > d) {
            colors[j + 1] = colors[j];
            depths[j + 1] = depths[j];
            --j;
        }
        colors[j + 1] = c;
        depths[j + 1] = d;
    }
    vec3 accum = vec3(0.0);
    float transmit = 1.0;
    for (int i = 0; i < n; ++i) {
        vec4 c = unpackUnorm4x8(colors[i]);
        accum += transmit * c.a * c.rgb;
        transmit *= 1.0 - c.a;
    }
    out_color = vec4(accum, transmit);
}
