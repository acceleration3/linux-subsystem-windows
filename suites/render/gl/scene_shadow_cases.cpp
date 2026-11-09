#include "render/gl/scene_extra.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6); }

constexpr float kUp[3] = {0.0f, 1.0f, 0.0f};

struct World {
    SceneMeshes meshes;

    void draw(Kit& kit, GLuint program, const Mat4& vp, bool colored) const {
        Painter p{kit};
        p.draw(program, meshes.quad, vp, Mat4::translate(0.0f, -1.0f, 0.0f) * Mat4::rotate_x(-1.5708f) * Mat4::scale(8.0f, 8.0f, 1.0f), 0.75f, 0.75f, 0.7f);
        p.draw(program, meshes.torus, vp, Mat4::translate(-1.2f, 0.0f, 0.2f) * Mat4::rotate_x(0.6f), colored ? 0.9f : 1.0f, 0.4f, 0.3f);
        p.draw(program, meshes.sphere, vp, Mat4::translate(1.3f, -0.2f, -0.6f) * Mat4::scale(0.8f, 0.8f, 0.8f), 0.3f, 0.7f, 0.9f);
        p.draw(program, meshes.cube, vp, Mat4::translate(0.2f, -0.5f, 1.6f) * Mat4::rotate_y(0.5f) * Mat4::scale(0.5f, 0.5f, 0.5f), 0.9f, 0.8f, 0.2f);
        p.draw(program, meshes.cone, vp, Mat4::translate(-0.2f, -1.0f, -1.8f), 0.4f, 0.9f, 0.4f);
    }
};

const char* kShadowFragment =
    "#version 460 core\n"
    "in vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\n"
    "uniform sampler2DShadow u_shadow;\n"
    "uniform mat4 u_light_vp;\n"
    "uniform vec4 u_color;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_nrm);\n"
    "    vec3 l = normalize(vec3(3.0, 5.0, 2.0));\n"
    "    vec4 lp = u_light_vp * vec4(v_world, 1.0);\n"
    "    vec3 c = lp.xyz / lp.w * 0.5 + 0.5;\n"
    "    float lit = 0.0;\n"
    "    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)\n"
    "        lit += texture(u_shadow, vec3(c.xy + vec2(float(x), float(y)) / 256.0, c.z - 0.002));\n"
    "    lit /= 9.0;\n"
    "    float d = max(dot(n, l), 0.0);\n"
    "    out_color = vec4(u_color.rgb * (0.18 + 0.82 * d * lit), 1.0);\n"
    "}\n";

const char* kCsmFragment =
    "#version 460 core\n"
    "in vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\n"
    "uniform sampler2DArrayShadow u_shadow;\n"
    "uniform mat4 u_light_vp[3];\n"
    "uniform vec4 u_color;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_nrm);\n"
    "    vec3 l = normalize(vec3(3.0, 5.0, 2.0));\n"
    "    int layer = v_depth < 4.0 ? 0 : (v_depth < 8.0 ? 1 : 2);\n"
    "    vec4 lp = u_light_vp[layer] * vec4(v_world, 1.0);\n"
    "    vec3 c = lp.xyz / lp.w * 0.5 + 0.5;\n"
    "    float lit = texture(u_shadow, vec4(c.xy, float(layer), c.z - 0.0015));\n"
    "    float d = max(dot(n, l), 0.0);\n"
    "    vec3 tint = layer == 0 ? vec3(1.0, 0.92, 0.92) : (layer == 1 ? vec3(0.92, 1.0, 0.92) : vec3(0.92, 0.92, 1.0));\n"
    "    out_color = vec4(u_color.rgb * tint * (0.18 + 0.82 * d * lit), 1.0);\n"
    "}\n";

const struct ShadowBatch {
    ShadowBatch() {
        Cases::single(scene("shadow_map_depth_texture_pcf"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH_COMPONENT24;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            RenderTarget shadow;
            if (!shadow.create(kit, 256, 256, {}, glc::DEPTH_COMPONENT24, error, glc::LINEAR)) {
                return false;
            }
            World world;
            world.meshes.create(kit);
            const GLuint depth_program = kit.program(ShadowSource::kDepthVertex, ShadowSource::kDepthFragment, error);
            const GLuint main_program = kit.program(ShadowSource::kLitVertex, kShadowFragment, error);
            if (depth_program == 0 || main_program == 0) {
                return false;
            }
            const float eye[3] = {3.0f, 5.0f, 2.0f};
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            const Mat4 light_vp = Gx::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 14.0f) * Xf::look_at(eye, origin, kUp);
            shadow.bind(kit);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glDepthMask(1);
            gl.glClearDepth(1.0);
            gl.glClear(glc::DEPTH_BUFFER_BIT);
            gl.glEnable(glc::POLYGON_OFFSET_FILL);
            gl.glPolygonOffset(1.5f, 4.0f);
            world.draw(kit, depth_program, light_vp, false);
            gl.glDisable(glc::POLYGON_OFFSET_FILL);
            gl.glBindTexture(glc::TEXTURE_2D, shadow.depth);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(glc::LEQUAL));
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
            Painter p{kit};
            p.bind_texture(0, glc::TEXTURE_2D, shadow.depth);
            gl.glUseProgram(main_program);
            p.integer(main_program, "u_shadow", 0);
            p.matrix(main_program, "u_light_vp", light_vp);
            const Mat4 view = Gx::camera(1.0f, 7.0f, 0.5f, 0.6f);
            p.matrix(main_program, "u_view", Mat4::translate(0, 0, -7.0f) * Mat4::rotate_x(0.6f) * Mat4::rotate_y(0.5f));
            world.draw(kit, main_program, view, true);
            if (!kit.check("shadow map", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(scene("shadow_cascaded_three_layers"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH_COMPONENT24;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            GLuint array = 0;
            GLuint fbo = 0;
            gl.glGenTextures(1, &array);
            gl.glGenFramebuffers(1, &fbo);
            kit.defer([&kit, array, fbo] {
                kit.gl.glDeleteTextures(1, &array);
                kit.gl.glDeleteFramebuffers(1, &fbo);
            });
            gl.glBindTexture(glc::TEXTURE_2D_ARRAY, array);
            gl.glTexStorage3D(glc::TEXTURE_2D_ARRAY, 1, glc::DEPTH_COMPONENT32F, 256, 256, 3);
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            World world;
            world.meshes.create(kit);
            const GLuint depth_program = kit.program(ShadowSource::kDepthVertex, ShadowSource::kDepthFragment, error);
            const GLuint main_program = kit.program(ShadowSource::kLitVertex, kCsmFragment, error);
            if (depth_program == 0 || main_program == 0) {
                return false;
            }
            const float eye[3] = {3.0f, 5.0f, 2.0f};
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            const float extents[3] = {2.8f, 5.5f, 11.0f};
            Mat4 light_vp[3];
            gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            gl.glDrawBuffer(glc::NONE);
            gl.glReadBuffer(glc::NONE);
            gl.glViewport(0, 0, 256, 256);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glEnable(glc::POLYGON_OFFSET_FILL);
            gl.glPolygonOffset(1.5f, 4.0f);
            for (int c = 0; c < 3; ++c) {
                light_vp[c] = Gx::ortho(-extents[c], extents[c], -extents[c], extents[c], 1.0f, 16.0f) * Xf::look_at(eye, origin, kUp);
                gl.glFramebufferTextureLayer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, array, 0, c);
                gl.glClear(glc::DEPTH_BUFFER_BIT);
                world.draw(kit, depth_program, light_vp[c], false);
            }
            gl.glDisable(glc::POLYGON_OFFSET_FILL);
            if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "cascade framebuffer incomplete";
                return false;
            }
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_COMPARE_MODE, static_cast<GLint>(glc::COMPARE_REF_TO_TEXTURE));
            gl.glTexParameteri(glc::TEXTURE_2D_ARRAY, glc::TEXTURE_COMPARE_FUNC, static_cast<GLint>(glc::LEQUAL));
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
            Painter p{kit};
            p.bind_texture(0, glc::TEXTURE_2D_ARRAY, array);
            gl.glUseProgram(main_program);
            p.integer(main_program, "u_shadow", 0);
            for (int c = 0; c < 3; ++c) {
                const std::string name = "u_light_vp[" + std::to_string(c) + "]";
                p.matrix(main_program, name.c_str(), light_vp[c]);
            }
            const Mat4 view_only = Mat4::translate(0, 0, -6.0f) * Mat4::rotate_x(0.45f) * Mat4::rotate_y(0.9f);
            p.matrix(main_program, "u_view", view_only);
            const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.5f, 30.0f, false) * view_only;
            world.draw(kit, main_program, vp, true);
            for (int i = 0; i < 4; ++i) {
                p.draw(main_program, world.meshes.sphere, vp, Mat4::translate(-3.0f + 2.0f * static_cast<float>(i), -0.4f, -3.5f - 1.6f * static_cast<float>(i)) * Mat4::scale(0.6f, 0.6f, 0.6f), 0.9f, 0.5f, 0.6f);
            }
            if (!kit.check("cascaded shadow", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(scene("shadow_omni_layered_cube_distance"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH_COMPONENT24;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            GLuint cube = 0;
            GLuint cube_depth = 0;
            GLuint fbo = 0;
            gl.glGenTextures(1, &cube);
            gl.glGenTextures(1, &cube_depth);
            gl.glGenFramebuffers(1, &fbo);
            kit.defer([&kit, cube, cube_depth, fbo] {
                kit.gl.glDeleteTextures(1, &cube);
                kit.gl.glDeleteTextures(1, &cube_depth);
                kit.gl.glDeleteFramebuffers(1, &fbo);
            });
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, cube);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::R32F, 128, 128);
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, cube_depth);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::DEPTH_COMPONENT24, 128, 128);
            gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            gl.glFramebufferTexture(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, cube, 0);
            gl.glFramebufferTexture(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, cube_depth, 0);
            const GLenum attachment = glc::COLOR_ATTACHMENT0;
            gl.glDrawBuffers(1, &attachment);
            if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                error = "layered cube framebuffer incomplete";
                return false;
            }
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec3 in_pos;\nuniform mat4 u_model;\nout vec3 g_world;\n"
                "void main() { g_world = (u_model * vec4(in_pos, 1.0)).xyz; gl_Position = vec4(g_world, 1.0); }\n";
            static const char* gs =
                "#version 460 core\nlayout(triangles, invocations = 6) in;\nlayout(triangle_strip, max_vertices = 3) out;\n"
                "in vec3 g_world[];\nuniform mat4 u_faces[6];\nout vec3 v_world;\n"
                "void main() {\n"
                "    for (int i = 0; i < 3; ++i) {\n"
                "        gl_Layer = gl_InvocationID;\n"
                "        v_world = g_world[i];\n"
                "        gl_Position = u_faces[gl_InvocationID] * vec4(g_world[i], 1.0);\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 v_world;\nuniform vec3 u_light;\nlayout(location = 0) out float out_dist;\n"
                "void main() { out_dist = length(v_world - u_light); }\n";
            const GLuint depth_program = kit.program({{glc::VERTEX_SHADER, vs}, {glc::GEOMETRY_SHADER, gs}, {glc::FRAGMENT_SHADER, fs}}, error);
            static const char* main_fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\nuniform samplerCube u_cube;\nuniform vec3 u_light;\nuniform vec4 u_color;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec3 to = v_world - u_light;\n"
                "    float stored = texture(u_cube, to).r;\n"
                "    float shadow = length(to) - 0.06 > stored ? 0.0 : 1.0;\n"
                "    vec3 n = normalize(v_nrm);\n"
                "    float att = 1.0 / (1.0 + 0.12 * dot(to, to));\n"
                "    float d = abs(dot(n, normalize(to)));\n"
                "    out_color = vec4(u_color.rgb * (0.12 + 1.6 * d * att * shadow), 1.0);\n"
                "}\n";
            const GLuint main_program = kit.program(ShadowSource::kLitVertex, main_fs, error);
            if (depth_program == 0 || main_program == 0) {
                return false;
            }
            const float light[3] = {0.3f, 1.4f, 0.2f};
            const Mat4 proj = Mat4::perspective(1.5707964f, 1.0f, 0.1f, 20.0f, false);
            const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const float ups[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            Mat4 faces[6];
            for (int i = 0; i < 6; ++i) {
                const float at[3] = {light[0] + dirs[i][0], light[1] + dirs[i][1], light[2] + dirs[i][2]};
                faces[i] = proj * Xf::look_at(light, at, ups[i]);
            }
            Painter p{kit};
            SceneMeshes meshes;
            meshes.create(kit);
            gl.glViewport(0, 0, 128, 128);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glDepthFunc(glc::LESS);
            gl.glDisable(glc::CULL_FACE);
            const GLfloat far_value[4] = {100.0f, 0, 0, 0};
            gl.glClearBufferfv(glc::COLOR, 0, far_value);
            gl.glClearDepth(1.0);
            gl.glClear(glc::DEPTH_BUFFER_BIT);
            gl.glUseProgram(depth_program);
            for (int i = 0; i < 6; ++i) {
                const std::string name = "u_faces[" + std::to_string(i) + "]";
                p.matrix(depth_program, name.c_str(), faces[i]);
            }
            p.vec3(depth_program, "u_light", light[0], light[1], light[2]);
            const Mat4 room = Mat4::translate(0.0f, 1.0f, 0.0f) * Mat4::scale(4.0f, 3.0f, 4.0f);
            const Mat4 models[4] = {room, Mat4::translate(0.9f, 0.0f, 0.5f) * Mat4::rotate_y(0.6f) * Mat4::scale(0.5f, 0.9f, 0.5f), Mat4::translate(-1.3f, 0.2f, -0.6f) * Mat4::scale(0.5f, 0.5f, 0.5f),
                                    Mat4::translate(0.2f, -0.7f, 1.3f) * Mat4::scale(0.35f, 0.35f, 0.35f)};
            const GpuShape* shapes[4] = {&meshes.cube, &meshes.cube, &meshes.sphere, &meshes.sphere};
            for (int i = 0; i < 4; ++i) {
                p.matrix(depth_program, "u_model", models[i]);
                shapes[i]->draw(kit);
            }
            target.bind(kit);
            Gx::clear(kit, 0.02f, 0.02f, 0.04f, 1.0f, true, true);
            p.bind_texture(0, glc::TEXTURE_CUBE_MAP, cube);
            gl.glUseProgram(main_program);
            p.integer(main_program, "u_cube", 0);
            p.vec3(main_program, "u_light", light[0], light[1], light[2]);
            const Mat4 view_only = Mat4::translate(0, 0, -0.6f) * Mat4::rotate_x(0.3f) * Mat4::rotate_y(0.7f) * Mat4::translate(0.0f, -0.6f, 0.0f);
            p.matrix(main_program, "u_view", view_only);
            const Mat4 vp = Mat4::perspective(1.4f, 1.0f, 0.1f, 30.0f, false) * view_only;
            for (int i = 0; i < 4; ++i) {
                const float c = 0.5f + 0.12f * static_cast<float>(i);
                p.draw(main_program, *shapes[i], vp, models[i], c, 0.9f - 0.15f * static_cast<float>(i), 0.45f);
            }
            if (!kit.check("omni shadow", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });
    }
} kShadowBatch;

}  // namespace

}  // namespace lsw::render::gl
