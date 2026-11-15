#version 450
layout(set = 0, binding = 0) buffer Head { uint head[]; };
layout(set = 0, binding = 1) buffer Nodes { uvec4 nodes[]; };
layout(set = 0, binding = 2) buffer Counter { uint count; };
layout(push_constant) uniform P { mat4 vp; vec4 params; vec4 color; } pc;
layout(location = 0) in vec3 v_n;
layout(location = 1) in vec3 v_c;
layout(location = 2) in vec3 v_world;
layout(early_fragment_tests) in;
void main() {
    uint idx = atomicAdd(count, 1u);
    if (idx < uint(pc.params.z)) {
        uint pix = uint(gl_FragCoord.y) * uint(pc.params.x) + uint(gl_FragCoord.x);
        float ndl = max(abs(dot(normalize(v_n), normalize(vec3(0.4, 0.8, 0.5)))), 0.0);
        vec4 c = vec4(pc.color.rgb * (0.4 + 0.6 * ndl), pc.color.a);
        uint prev = atomicExchange(head[pix], idx);
        nodes[idx] = uvec4(packUnorm4x8(c), floatBitsToUint(gl_FragCoord.z), prev, 0u);
    }
}
