#include "render/gl/scene_extra.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6); }

const char* kFaceVertex =
    "#version 460 core\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char* kFaceDirection =
    "uniform int u_face;\n"
    "vec3 face_dir(vec2 st) {\n"
    "    vec2 f = st * 2.0 - 1.0;\n"
    "    if (u_face == 0) return vec3(1.0, -f.y, -f.x);\n"
    "    if (u_face == 1) return vec3(-1.0, -f.y, f.x);\n"
    "    if (u_face == 2) return vec3(f.x, 1.0, f.y);\n"
    "    if (u_face == 3) return vec3(f.x, -1.0, -f.y);\n"
    "    if (u_face == 4) return vec3(f.x, -f.y, 1.0);\n"
    "    return vec3(-f.x, -f.y, -1.0);\n"
    "}\n";

const struct PbrBatch {
    PbrBatch() {
        Cases::single(scene("pbr_ibl_sphere_grid").tolerance(Tolerance::filter(4, 0.002)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Api& gl = kit.gl;
            Target target;
            TargetDesc desc;
            desc.depth = glc::DEPTH_COMPONENT24;
            if (!target.create(kit, desc, error)) {
                return false;
            }
            GLuint env = 0;
            GLuint irradiance = 0;
            GLuint fbo = 0;
            gl.glGenTextures(1, &env);
            gl.glGenTextures(1, &irradiance);
            gl.glGenFramebuffers(1, &fbo);
            kit.defer([&kit, env, irradiance, fbo] {
                kit.gl.glDeleteTextures(1, &env);
                kit.gl.glDeleteTextures(1, &irradiance);
                kit.gl.glDeleteFramebuffers(1, &fbo);
            });
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, env);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 5, glc::RGBA16F, 64, 64);
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, irradiance);
            gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA16F, 16, 16);
            gl.glEnable(glc::TEXTURE_CUBE_MAP_SEAMLESS);
            const std::string sky_fs = std::string("#version 460 core\n") + kFaceDirection +
                                       "layout(location = 0) out vec4 out_color;\n"
                                       "void main() {\n"
                                       "    vec3 d = normalize(face_dir(gl_FragCoord.xy / 64.0));\n"
                                       "    vec3 sky = mix(vec3(0.55, 0.5, 0.45), vec3(0.15, 0.3, 0.7), clamp(d.y * 0.5 + 0.5, 0.0, 1.0));\n"
                                       "    float sun = pow(max(dot(d, normalize(vec3(0.5, 0.6, 0.4))), 0.0), 64.0) * 18.0;\n"
                                       "    float band = d.y < 0.0 ? 0.15 : 0.0;\n"
                                       "    out_color = vec4(sky * (1.0 - band) + vec3(1.0, 0.85, 0.6) * sun, 1.0);\n"
                                       "}\n";
            const std::string conv_fs = std::string("#version 460 core\n") + kFaceDirection +
                                        "uniform samplerCube u_env;\n"
                                        "layout(location = 0) out vec4 out_color;\n"
                                        "void main() {\n"
                                        "    vec3 n = normalize(face_dir(gl_FragCoord.xy / 16.0));\n"
                                        "    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
                                        "    vec3 t = normalize(cross(up, n));\n"
                                        "    vec3 b = cross(n, t);\n"
                                        "    vec3 sum = vec3(0.0);\n"
                                        "    for (int i = 0; i < 64; ++i) {\n"
                                        "        float u1 = (float(i) + 0.5) / 64.0;\n"
                                        "        float phi = 2.399963 * float(i);\n"
                                        "        float r = sqrt(u1);\n"
                                        "        vec3 d = t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(1.0 - u1);\n"
                                        "        sum += textureLod(u_env, d, 2.0).rgb;\n"
                                        "    }\n"
                                        "    out_color = vec4(sum / 64.0, 1.0);\n"
                                        "}\n";
            const GLuint sky = kit.program(kFaceVertex, sky_fs.c_str(), error);
            const GLuint conv = kit.program(kFaceVertex, conv_fs.c_str(), error);
            static const char* pbr_fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec3 v_world;\nin vec2 v_uv;\nin float v_depth;\n"
                "uniform samplerCube u_env;\nuniform samplerCube u_irradiance;\nuniform vec3 u_eye;\nuniform vec4 u_color;\nuniform vec2 u_material;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
                "void main() {\n"
                "    vec3 n = normalize(v_nrm);\n"
                "    vec3 v = normalize(u_eye - v_world);\n"
                "    float metal = u_material.x;\n"
                "    float rough = max(u_material.y, 0.05);\n"
                "    vec3 f0 = mix(vec3(0.04), u_color.rgb, metal);\n"
                "    float ndv = max(dot(n, v), 0.0);\n"
                "    vec3 fres = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - ndv, 5.0);\n"
                "    vec3 diffuse = texture(u_irradiance, n).rgb * u_color.rgb * (1.0 - metal) * (1.0 - fres);\n"
                "    vec3 r = reflect(-v, n);\n"
                "    vec3 spec = textureLod(u_env, r, rough * 4.0).rgb * fres;\n"
                "    vec3 l = normalize(vec3(0.5, 0.6, 0.4));\n"
                "    vec3 h = normalize(l + v);\n"
                "    float a2 = pow(rough, 4.0);\n"
                "    float nh = max(dot(n, h), 0.0);\n"
                "    float dd = nh * nh * (a2 - 1.0) + 1.0;\n"
                "    vec3 direct = (u_color.rgb * (1.0 - metal) / 3.14159 + f0 * a2 / (3.14159 * dd * dd)) * max(dot(n, l), 0.0) * 3.0;\n"
                "    out_color = vec4(pow(aces(diffuse + spec + direct), vec3(1.0 / 2.2)), 1.0);\n"
                "}\n";
            const GLuint pbr = kit.program(ShadowSource::kLitVertex, pbr_fs, error);
            if (sky == 0 || conv == 0 || pbr == 0) {
                return false;
            }
            gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
            gl.glBindVertexArray(kit.vertex_array());
            gl.glDisable(glc::DEPTH_TEST);
            Painter p{kit};
            gl.glViewport(0, 0, 64, 64);
            gl.glUseProgram(sky);
            for (unsigned face = 0; face < 6; ++face) {
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, env, 0);
                p.integer(sky, "u_face", static_cast<GLint>(face));
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            }
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, env);
            gl.glGenerateMipmap(glc::TEXTURE_CUBE_MAP);
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR_MIPMAP_LINEAR));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_R, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glViewport(0, 0, 16, 16);
            p.bind_texture(0, glc::TEXTURE_CUBE_MAP, env);
            gl.glUseProgram(conv);
            p.integer(conv, "u_env", 0);
            for (unsigned face = 0; face < 6; ++face) {
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, irradiance, 0);
                p.integer(conv, "u_face", static_cast<GLint>(face));
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            }
            gl.glBindTexture(glc::TEXTURE_CUBE_MAP, irradiance);
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_WRAP_R, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            if (!kit.check("ibl precompute", error)) {
                return false;
            }
            SceneMeshes meshes;
            meshes.create(kit);
            target.bind(kit);
            Gx::clear(kit, 0.05f, 0.05f, 0.07f, 1.0f, true, true);
            gl.glEnable(glc::DEPTH_TEST);
            p.bind_texture(0, glc::TEXTURE_CUBE_MAP, env);
            p.bind_texture(1, glc::TEXTURE_CUBE_MAP, irradiance);
            gl.glUseProgram(pbr);
            p.integer(pbr, "u_env", 0);
            p.integer(pbr, "u_irradiance", 1);
            p.vec3(pbr, "u_eye", 0.0f, 0.0f, 6.0f);
            const Mat4 vp = Mat4::perspective(0.9f, 1.0f, 0.5f, 30.0f, false) * Mat4::translate(0, 0, -6.0f);
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const GLint material = gl.glGetUniformLocation(pbr, "u_material");
                    gl.glUniform2f(material, static_cast<float>(row) * 0.5f, 0.12f + 0.38f * static_cast<float>(col));
                    p.draw(pbr, meshes.sphere, vp, Mat4::translate(-1.7f + 1.7f * static_cast<float>(col), 1.7f - 1.7f * static_cast<float>(row), 0.0f) * Mat4::scale(0.7f, 0.7f, 0.7f), 0.95f, 0.65f - 0.2f * static_cast<float>(row), 0.35f);
                }
            }
            if (!kit.check("pbr", error)) {
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });
    }
} kPbrBatch;

}  // namespace

}  // namespace lsw::render::gl
