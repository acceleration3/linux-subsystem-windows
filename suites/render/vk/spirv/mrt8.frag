#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
layout(location = 4) out vec4 o4;
layout(location = 5) out vec4 o5;
layout(location = 6) out vec4 o6;
layout(location = 7) out vec4 o7;
void main() {
    o0 = vec4(v_color.rgb, 0.5);
    o1 = vec4(v_color.gbr, 0.6);
    o2 = vec4(v_color.brg, 0.7);
    o3 = vec4(v_color.rgb * 0.5, 0.8);
    o4 = vec4(1.0 - v_color.rgb, 0.4);
    o5 = vec4(v_color.rrr, 0.9);
    o6 = vec4(v_color.ggg, 0.3);
    o7 = vec4(v_color.bbb, 1.0);
}
