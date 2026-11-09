#version 450
layout(set = 0, binding = 0) uniform samplerCube u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec2 cell = vec2(floor(v_uv.x * 3.0), floor(v_uv.y * 2.0));
    vec2 l = fract(vec2(v_uv.x * 3.0, v_uv.y * 2.0)) * 2.0 - 1.0;
    int face = int(cell.x + cell.y * 3.0);
    vec3 dir;
    if (face == 0) dir = vec3(1.0, -l.y, -l.x);
    else if (face == 1) dir = vec3(-1.0, -l.y, l.x);
    else if (face == 2) dir = vec3(l.x, 1.0, l.y);
    else if (face == 3) dir = vec3(l.x, -1.0, -l.y);
    else if (face == 4) dir = vec3(l.x, -l.y, 1.0);
    else dir = vec3(-l.x, -l.y, -1.0);
    out_color = texture(u_tex, dir);
}
