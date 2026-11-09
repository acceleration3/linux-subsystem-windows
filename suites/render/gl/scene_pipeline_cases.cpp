#include "render/gl/scene_extra.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6); }

const char* kGeometryFragment =
    "#version 460 core\n"
    "in vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\n"
    "uniform vec4 u_color;\n"
    "layout(location = 0) out vec4 out_albedo;\n"
    "layout(location = 1) out vec4 out_normal;\n"
    "void main() {\n"
    "    float checker = mod(floor(v_uv.x * 8.0) + floor(v_uv.y * 8.0), 2.0);\n"
    "    out_albedo = vec4(u_color.rgb * (0.7 + 0.3 * checker), 0.4 + 0.5 * checker);\n"
    "    out_normal = vec4(normalize(v_nrm), 1.0);\n"
    "}\n";

const char* kFullscreenVertex =
    "#version 460 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    v_uv = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

void draw_objects(Kit& kit, const SceneMeshes& m, GLuint program, const Mat4& vp, const Mat4& view) {
    Painter p{kit};
    p.matrix(program, "u_view", view);
    p.draw(program, m.quad, vp, Mat4::translate(0.0f, -1.0f, 0.0f) * Mat4::rotate_x(-1.5708f) * Mat4::scale(5.0f, 5.0f, 1.0f), 0.7f, 0.72f, 0.75f);
    p.draw(program, m.sphere, vp, Mat4::translate(-1.3f, -0.1f, 0.0f), 0.9f, 0.35f, 0.3f);
    p.draw(program, m.torus, vp, Mat4::translate(0.9f, 0.0f, -0.5f) * Mat4::rotate_x(1.1f), 0.3f, 0.8f, 0.4f);
    p.draw(program, m.cube, vp, Mat4::translate(0.1f, -0.5f, 1.4f) * Mat4::rotate_y(0.7f) * Mat4::scale(0.5f, 0.5f, 0.5f), 0.9f, 0.8f, 0.3f);
    p.draw(program, m.cone, vp, Mat4::translate(-0.6f, -1.0f, -1.7f), 0.35f, 0.5f, 0.95f);
}

const struct PipelineBatch {
    PipelineBatch() {
        Cases::single(scene("deferred_gbuffer_point_lights"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            RenderTarget g;
            if (!g.create(kit, 128, 128, {glc::RGBA8, glc::RGBA16F}, glc::DEPTH_COMPONENT32F, error)) {
                return false;
            }
            SceneMeshes meshes;
            meshes.create(kit);
            const GLuint geometry = kit.program(ShadowSource::kLitVertex, kGeometryFragment, error);
            static const char* light_fs =
                "#version 460 core\nin vec2 v_uv;\nuniform sampler2D u_albedo;\nuniform sampler2D u_normal;\nuniform sampler2D u_depth;\nuniform mat4 u_inv_vp;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 uv = gl_FragCoord.xy / 128.0;\n"
                "    float z = texture(u_depth, uv).r;\n"
                "    if (z >= 1.0) { out_color = vec4(0.05, 0.05, 0.09, 1.0); return; }\n"
                "    vec4 clip = vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);\n"
                "    vec4 w = u_inv_vp * clip;\n"
                "    vec3 pos = w.xyz / w.w;\n"
                "    vec4 a = texture(u_albedo, uv);\n"
                "    vec3 n = normalize(texture(u_normal, uv).xyz);\n"
                "    vec3 c = a.rgb * 0.08;\n"
                "    for (int i = 0; i < 8; ++i) {\n"
                "        float t = float(i) * 0.785398;\n"
                "        vec3 lp = vec3(2.2 * cos(t), 0.2 + 0.5 * sin(t * 3.0), 2.2 * sin(t));\n"
                "        vec3 lc = vec3(0.5 + 0.5 * cos(t * 2.0), 0.5 + 0.5 * sin(t * 2.0 + 1.0), 0.5 + 0.5 * cos(t + 2.0));\n"
                "        vec3 d = lp - pos;\n"
                "        float dist = length(d);\n"
                "        float att = 1.0 / (1.0 + 0.8 * dist * dist);\n"
                "        float diff = max(dot(n, d / dist), 0.0);\n"
                "        vec3 h = normalize(d / dist + vec3(0.0, 0.6, 0.8));\n"
                "        float spec = pow(max(dot(n, h), 0.0), 24.0) * a.a;\n"
                "        c += lc * att * (a.rgb * diff + spec);\n"
                "    }\n"
                "    out_color = vec4(c * 1.8, 1.0);\n"
                "}\n";
            const GLuint light = kit.program(kFullscreenVertex, light_fs, error);
            if (geometry == 0 || light == 0) {
                return false;
            }
            const Mat4 view = Mat4::translate(0, 0, -6.0f) * Mat4::rotate_x(0.5f) * Mat4::rotate_y(0.4f);
            const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.5f, 30.0f, false) * view;
            g.bind(kit);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glDepthMask(1);
            const GLfloat zero[4] = {0, 0, 0, 0};
            gl.glClearBufferfv(glc::COLOR, 0, zero);
            gl.glClearBufferfv(glc::COLOR, 1, zero);
            gl.glClearDepth(1.0);
            gl.glClear(glc::DEPTH_BUFFER_BIT);
            draw_objects(kit, meshes, geometry, vp, view);
            target.bind(kit);
            gl.glDisable(glc::DEPTH_TEST);
            Painter p{kit};
            p.bind_texture(0, glc::TEXTURE_2D, g.color[0]);
            p.bind_texture(1, glc::TEXTURE_2D, g.color[1]);
            p.bind_texture(2, glc::TEXTURE_2D, g.depth);
            gl.glUseProgram(light);
            p.integer(light, "u_albedo", 0);
            p.integer(light, "u_normal", 1);
            p.integer(light, "u_depth", 2);
            p.matrix(light, "u_inv_vp", Xf::inverse(vp));
            gl.glBindVertexArray(kit.vertex_array());
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            if (!kit.check("deferred", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(scene("ssao_hemisphere_blur_composite"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            RenderTarget g;
            RenderTarget ao;
            RenderTarget blur;
            if (!g.create(kit, 128, 128, {glc::RGBA8, glc::RGBA16F}, glc::DEPTH_COMPONENT32F, error) || !ao.create(kit, 128, 128, {glc::R8}, 0, error) ||
                !blur.create(kit, 128, 128, {glc::R8}, 0, error)) {
                return false;
            }
            SceneMeshes meshes;
            meshes.create(kit);
            const GLuint geometry = kit.program(ShadowSource::kLitVertex, kGeometryFragment, error);
            static const char* ssao_fs =
                "#version 460 core\nuniform sampler2D u_normal;\nuniform sampler2D u_depth;\nuniform mat4 u_proj;\nuniform mat4 u_inv_proj;\nuniform mat3 u_view_normal;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "float hash(float n) { return fract(sin(n) * 43758.5453); }\n"
                "vec3 view_pos(vec2 uv) {\n"
                "    float z = texture(u_depth, uv).r;\n"
                "    vec4 v = u_inv_proj * vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);\n"
                "    return v.xyz / v.w;\n"
                "}\n"
                "void main() {\n"
                "    vec2 uv = gl_FragCoord.xy / 128.0;\n"
                "    if (texture(u_depth, uv).r >= 1.0) { out_color = vec4(1.0); return; }\n"
                "    vec3 p = view_pos(uv);\n"
                "    vec3 n = normalize(u_view_normal * texture(u_normal, uv).xyz);\n"
                "    float seed = dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715));\n"
                "    float ang = 6.2831853 * fract(52.9829189 * fract(seed));\n"
                "    vec3 t = normalize(abs(n.y) < 0.9 ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));\n"
                "    vec3 b = cross(n, t);\n"
                "    t = t * cos(ang) + b * sin(ang);\n"
                "    b = cross(n, t);\n"
                "    float occ = 0.0;\n"
                "    for (int i = 0; i < 16; ++i) {\n"
                "        float fi = float(i);\n"
                "        float u1 = (fi + 0.5) / 16.0;\n"
                "        float u2 = hash(fi * 7.31 + 1.7);\n"
                "        float r = sqrt(u1);\n"
                "        vec3 dir = t * (r * cos(6.2831853 * u2)) + b * (r * sin(6.2831853 * u2)) + n * sqrt(1.0 - u1);\n"
                "        float scale = mix(0.1, 1.0, u1 * u1);\n"
                "        vec3 sp = p + dir * 0.7 * scale;\n"
                "        vec4 pp = u_proj * vec4(sp, 1.0);\n"
                "        vec2 suv = pp.xy / pp.w * 0.5 + 0.5;\n"
                "        float sd = view_pos(suv).z;\n"
                "        float range = smoothstep(0.0, 1.0, 0.7 / abs(p.z - sd));\n"
                "        occ += (sd >= sp.z + 0.03 ? 1.0 : 0.0) * range;\n"
                "    }\n"
                "    out_color = vec4(vec3(1.0 - occ / 16.0), 1.0);\n"
                "}\n";
            static const char* blur_fs =
                "#version 460 core\nuniform sampler2D u_ao;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    float s = 0.0;\n"
                "    for (int y = -2; y <= 1; ++y) for (int x = -2; x <= 1; ++x) s += texelFetch(u_ao, ivec2(gl_FragCoord.xy) + ivec2(x, y), 0).r;\n"
                "    out_color = vec4(vec3(s / 16.0), 1.0);\n"
                "}\n";
            static const char* comp_fs =
                "#version 460 core\nuniform sampler2D u_albedo;\nuniform sampler2D u_normal;\nuniform sampler2D u_ao;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 uv = gl_FragCoord.xy / 128.0;\n"
                "    vec3 n = normalize(texture(u_normal, uv).xyz);\n"
                "    float d = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.3, 0.9, 0.5))), 0.0);\n"
                "    float a = texture(u_ao, uv).r;\n"
                "    out_color = vec4(texture(u_albedo, uv).rgb * d * a, 1.0);\n"
                "}\n";
            const GLuint ssao = kit.program(kFullscreenVertex, ssao_fs, error);
            const GLuint blur_program = kit.program(kFullscreenVertex, blur_fs, error);
            const GLuint comp = kit.program(kFullscreenVertex, comp_fs, error);
            if (geometry == 0 || ssao == 0 || blur_program == 0 || comp == 0) {
                return false;
            }
            const Mat4 view = Mat4::translate(0, 0, -5.5f) * Mat4::rotate_x(0.5f) * Mat4::rotate_y(0.4f);
            const Mat4 proj = Mat4::perspective(1.0f, 1.0f, 0.5f, 30.0f, false);
            g.bind(kit);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glDepthMask(1);
            const GLfloat zero[4] = {0, 0, 0, 0};
            gl.glClearBufferfv(glc::COLOR, 0, zero);
            gl.glClearBufferfv(glc::COLOR, 1, zero);
            gl.glClearDepth(1.0);
            gl.glClear(glc::DEPTH_BUFFER_BIT);
            draw_objects(kit, meshes, geometry, proj * view, view);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glBindVertexArray(kit.vertex_array());
            Painter p{kit};
            ao.bind(kit);
            p.bind_texture(0, glc::TEXTURE_2D, g.color[1]);
            p.bind_texture(1, glc::TEXTURE_2D, g.depth);
            gl.glUseProgram(ssao);
            p.integer(ssao, "u_normal", 0);
            p.integer(ssao, "u_depth", 1);
            p.matrix(ssao, "u_proj", proj);
            p.matrix(ssao, "u_inv_proj", Xf::inverse(proj));
            const GLint normal_location = gl.glGetUniformLocation(ssao, "u_view_normal");
            const float vn[9] = {view.m[0], view.m[1], view.m[2], view.m[4], view.m[5], view.m[6], view.m[8], view.m[9], view.m[10]};
            gl.glUniformMatrix3fv(normal_location, 1, glc::FALSE_, vn);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            blur.bind(kit);
            p.bind_texture(0, glc::TEXTURE_2D, ao.color[0]);
            gl.glUseProgram(blur_program);
            p.integer(blur_program, "u_ao", 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            target.bind(kit);
            p.bind_texture(0, glc::TEXTURE_2D, g.color[0]);
            p.bind_texture(1, glc::TEXTURE_2D, g.color[1]);
            p.bind_texture(2, glc::TEXTURE_2D, blur.color[0]);
            gl.glUseProgram(comp);
            p.integer(comp, "u_albedo", 0);
            p.integer(comp, "u_normal", 1);
            p.integer(comp, "u_ao", 2);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            if (!kit.check("ssao", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(scene("hdr_bloom_tonemap_chain"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            RenderTarget hdr;
            RenderTarget half[2];
            if (!hdr.create(kit, 128, 128, {glc::RGBA16F}, glc::DEPTH_COMPONENT24, error) || !half[0].create(kit, 64, 64, {glc::RGBA16F}, 0, error, glc::LINEAR) ||
                !half[1].create(kit, 64, 64, {glc::RGBA16F}, 0, error, glc::LINEAR)) {
                return false;
            }
            SceneMeshes meshes;
            meshes.create(kit);
            static const char* hdr_fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\nuniform vec4 u_color;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    float d = max(dot(normalize(v_nrm), normalize(vec3(0.4, 0.8, 0.5))), 0.0);\n"
                "    vec3 c = u_color.rgb * (0.1 + d);\n"
                "    if (u_color.a > 1.5) c = u_color.rgb * u_color.a * (0.8 + 0.2 * d);\n"
                "    out_color = vec4(c, 1.0);\n"
                "}\n";
            static const char* bright_fs =
                "#version 460 core\nuniform sampler2D u_src;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec3 c = texture(u_src, gl_FragCoord.xy / 64.0).rgb; float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); out_color = vec4(c * smoothstep(1.0, 2.5, l), 1.0); }\n";
            static const char* blur_fs =
                "#version 460 core\nuniform sampler2D u_src;\nuniform vec2 u_dir;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    const float w[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
                "    vec2 uv = gl_FragCoord.xy / 64.0;\n"
                "    vec3 c = texture(u_src, uv).rgb * w[0];\n"
                "    for (int i = 1; i < 5; ++i) c += (texture(u_src, uv + u_dir * float(i) / 64.0).rgb + texture(u_src, uv - u_dir * float(i) / 64.0).rgb) * w[i];\n"
                "    out_color = vec4(c, 1.0);\n"
                "}\n";
            static const char* comp_fs =
                "#version 460 core\nuniform sampler2D u_hdr;\nuniform sampler2D u_bloom;\nlayout(location = 0) out vec4 out_color;\n"
                "vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
                "void main() {\n"
                "    vec2 uv = gl_FragCoord.xy / 128.0;\n"
                "    vec3 c = texture(u_hdr, uv).rgb + texture(u_bloom, uv).rgb * 0.9;\n"
                "    out_color = vec4(pow(aces(c * 0.9), vec3(1.0 / 2.2)), 1.0);\n"
                "}\n";
            const GLuint hdr_program = kit.program(ShadowSource::kLitVertex, hdr_fs, error);
            const GLuint bright = kit.program(kFullscreenVertex, bright_fs, error);
            const GLuint blur_program = kit.program(kFullscreenVertex, blur_fs, error);
            const GLuint comp = kit.program(kFullscreenVertex, comp_fs, error);
            if (hdr_program == 0 || bright == 0 || blur_program == 0 || comp == 0) {
                return false;
            }
            const Mat4 view = Mat4::translate(0, 0, -6.0f) * Mat4::rotate_x(0.35f) * Mat4::rotate_y(0.3f);
            const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.5f, 30.0f, false) * view;
            hdr.bind(kit);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glDepthMask(1);
            const GLfloat clear[4] = {0.02f, 0.02f, 0.04f, 1.0f};
            gl.glClearBufferfv(glc::COLOR, 0, clear);
            gl.glClearDepth(1.0);
            gl.glClear(glc::DEPTH_BUFFER_BIT);
            Painter p{kit};
            p.matrix(hdr_program, "u_view", view);
            p.draw(hdr_program, meshes.quad, vp, Mat4::translate(0.0f, -1.0f, 0.0f) * Mat4::rotate_x(-1.5708f) * Mat4::scale(5.0f, 5.0f, 1.0f), 0.5f, 0.5f, 0.55f);
            p.draw(hdr_program, meshes.torus, vp, Mat4::translate(-0.2f, 0.1f, 0.0f) * Mat4::rotate_x(1.0f), 0.6f, 0.7f, 0.9f);
            const float emit[3][4] = {{6.0f, 2.0f, 0.5f, 6.0f}, {0.6f, 3.0f, 5.0f, 5.0f}, {5.0f, 5.0f, 5.0f, 8.0f}};
            for (int i = 0; i < 3; ++i) {
                p.draw(hdr_program, meshes.sphere, vp, Mat4::translate(-2.0f + 2.0f * static_cast<float>(i), 0.1f + 0.4f * static_cast<float>(i % 2), 0.9f - 1.3f * static_cast<float>(i)) * Mat4::scale(0.35f, 0.35f, 0.35f), emit[i][0], emit[i][1], emit[i][2], emit[i][3]);
            }
            gl.glDisable(glc::DEPTH_TEST);
            gl.glBindVertexArray(kit.vertex_array());
            half[0].bind(kit);
            p.bind_texture(0, glc::TEXTURE_2D, hdr.color[0]);
            gl.glUseProgram(bright);
            p.integer(bright, "u_src", 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glUseProgram(blur_program);
            p.integer(blur_program, "u_src", 0);
            for (int pass = 0; pass < 6; ++pass) {
                const int source = pass % 2;
                half[1 - source].bind(kit);
                p.bind_texture(0, glc::TEXTURE_2D, half[source].color[0]);
                const GLint dir = gl.glGetUniformLocation(blur_program, "u_dir");
                gl.glUniform2f(dir, pass % 2 == 0 ? 1.0f : 0.0f, pass % 2 == 0 ? 0.0f : 1.0f);
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            }
            target.bind(kit);
            p.bind_texture(0, glc::TEXTURE_2D, hdr.color[0]);
            p.bind_texture(1, glc::TEXTURE_2D, half[0].color[0]);
            gl.glUseProgram(comp);
            p.integer(comp, "u_hdr", 0);
            p.integer(comp, "u_bloom", 1);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            if (!kit.check("hdr bloom", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });
    }
} kPipelineBatch;

}  // namespace

}  // namespace lsw::render::gl
