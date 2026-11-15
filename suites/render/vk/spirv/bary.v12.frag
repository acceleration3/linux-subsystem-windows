#version 450
#extension GL_EXT_fragment_shader_barycentric : require
layout(location = 0) out vec4 out_color;
void main() {
    vec3 b = gl_BaryCoordEXT;
    out_color = vec4(b, 1.0);
}
