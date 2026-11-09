#version 450
layout(set = 0, binding = 0) uniform Block { vec4 tint; } ubo;
layout(set = 0, binding = 1, std430) readonly buffer Ssbo { vec4 values[]; } ssbo;
layout(set = 0, binding = 2) uniform sampler2D u_combined;
layout(set = 0, binding = 3) uniform texture2D u_image;
layout(set = 0, binding = 4) uniform sampler u_sampler;
layout(set = 0, binding = 5) uniform samplerBuffer u_texel;
layout(set = 0, binding = 6, rgba32f) uniform readonly imageBuffer u_storage_texel;
layout(set = 0, binding = 7, rgba8) uniform readonly image2D u_storage_image;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 cell = vec2(floor(v_uv.x * 4.0), floor(v_uv.y * 2.0));
    vec2 local = fract(vec2(v_uv.x * 4.0, v_uv.y * 2.0));
    int which = int(cell.x + cell.y * 4.0);
    vec4 c;
    if (which == 0) c = ubo.tint;
    else if (which == 1) c = ssbo.values[int(local.x * 4.0) + 4 * int(local.y * 2.0)];
    else if (which == 2) c = texture(u_combined, local);
    else if (which == 3) c = texture(sampler2D(u_image, u_sampler), local * 2.0);
    else if (which == 4) c = texelFetch(u_texel, int(local.x * 8.0));
    else if (which == 5) c = imageLoad(u_storage_texel, int(local.y * 8.0));
    else if (which == 6) c = imageLoad(u_storage_image, ivec2(local * 8.0));
    else c = ubo.tint.bgra * ssbo.values[1] + texelFetch(u_texel, 3);
    out_color = clamp(c, 0.0, 1.0);
}
