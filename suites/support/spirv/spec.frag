#version 450
layout(constant_id = 0) const float kRed = 0.0;
layout(constant_id = 1) const float kGreen = 0.0;
layout(constant_id = 2) const float kBlue = 0.0;
layout(location = 0) out vec4 c0;
void main() {
    c0 = vec4(kRed, kGreen, kBlue, 1.0);
}
