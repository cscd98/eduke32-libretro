#include "gles.h"

#include "core.h"
#include "glad/glad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using Matrix = std::array<float, 16>;

Matrix identity()
{
    Matrix m = {};
    m[0] = m[5] = m[10] = m[15] = 1;

    return m;
}

Matrix multiply(const Matrix &a, const Matrix &b)
{
    Matrix out = {};
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++)
                out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
}

std::vector<Matrix> model, projection, texture[5];
GLenum              matrixMode = GL_MODELVIEW, clientTexture = GL_TEXTURE0;

float  color[4] = { 1, 1, 1, 1 }, coords[5][4] = {}, fogColor[4] = {};
float  fogStart = 0, fogEnd = 1, alphaRef = 0;
GLenum alphaFunc = GL_GREATER;
bool   alphaTest = false;

GLuint immediateVao = 0, immediateBuffer = 0;
GLenum primitive = GL_TRIANGLES;

struct Vertex
{
    float pos[3], color[4], uv[5][4];
};

std::vector<Vertex>    vertices;
PFNGLENABLEPROC        enable;
PFNGLDISABLEPROC       disable;
PFNGLGETFLOATVPROC     getFloat;
PFNGLSHADERSOURCEPROC  shaderSource;
PFNGLDRAWARRAYSPROC    drawArrays;
PFNGLDRAWELEMENTSPROC  drawElements;
PFNGLHINTPROC          hint;
PFNGLCOMPILESHADERPROC compileShader;
PFNGLLINKPROGRAMPROC   linkProgram;
retro_log_printf_t     logMessage = nullptr;

struct Uniforms
{
    GLint model, mvp, tex, fogColor, fogEnd, fogScale, alpha;
};

std::unordered_map<GLuint, Uniforms> uniforms;

std::vector<Matrix> &stack()
{
    if (matrixMode == GL_PROJECTION)
        return projection;

    if (matrixMode == GL_TEXTURE)
    {
        GLint unit = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);

        return texture[std::max(0, std::min(4, unit - GL_TEXTURE0))];
    }

    return model;
}

void APIENTRY setMatrixMode(GLenum mode)
{
    matrixMode = mode;
}

void APIENTRY loadIdentity()
{
    stack().back() = identity();
}

void APIENTRY loadMatrix(const GLfloat *m)
{
    std::copy(m, m + 16, stack().back().begin());
}

void APIENTRY multMatrix(const GLfloat *m)
{
    Matrix b;
    std::copy(m, m + 16, b.begin());
    stack().back() = multiply(stack().back(), b);
}

void APIENTRY pushMatrix()
{
    auto &s = stack();

    s.push_back(s.back());
}

void APIENTRY popMatrix()
{
    auto &s = stack();

    if (s.size() > 1)
        s.pop_back();
}

void APIENTRY translate(GLfloat x, GLfloat y, GLfloat z)
{
    auto m = identity();
    m[12]  = x;
    m[13]  = y;
    m[14]  = z;

    multMatrix(m.data());
}

void APIENTRY scale(GLfloat x, GLfloat y, GLfloat z)
{
    auto m = identity();
    m[0]   = x;
    m[5]   = y;
    m[10]  = z;

    multMatrix(m.data());
}

void APIENTRY ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    auto m = identity();
    m[0]   = GLfloat(2 / (r - l));
    m[5]   = GLfloat(2 / (t - b));
    m[10]  = GLfloat(-2 / (f - n));
    m[12]  = GLfloat(-(r + l) / (r - l));
    m[13]  = GLfloat(-(t + b) / (t - b));
    m[14]  = GLfloat(-(f + n) / (f - n));

    multMatrix(m.data());
}

void APIENTRY floatState(GLenum key, GLfloat *v)
{
    if (key == GL_MODELVIEW_MATRIX)
        std::copy(model.back().begin(), model.back().end(), v);
    else if (key == GL_PROJECTION_MATRIX)
        std::copy(projection.back().begin(), projection.back().end(), v);
    else if (key == GL_CURRENT_COLOR)
        std::copy(color, color + 4, v);
    else
        getFloat(key, v);
}

void APIENTRY color4(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;

    glVertexAttrib4fv(2, color);
}

void APIENTRY color3(GLfloat r, GLfloat g, GLfloat b)
{
    color4(r, g, b, 1);
}

void APIENTRY colorBytes(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    color4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}

void APIENTRY clientUnit(GLenum unit)
{
    clientTexture = unit;
}

unsigned texAttribute()
{
    return clientTexture == GL_TEXTURE3 ? 3 : clientTexture == GL_TEXTURE4 ? 4 : 1;
}

void APIENTRY vertexPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    glVertexAttribPointer(0, size, type, GL_FALSE, stride, p);
}

void APIENTRY texPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    glVertexAttribPointer(texAttribute(), size, type, GL_FALSE, stride, p);
}

void APIENTRY clientEnable(GLenum key)
{
    if (key == GL_VERTEX_ARRAY)
        glEnableVertexAttribArray(0);
    else if (key == GL_TEXTURE_COORD_ARRAY)
        glEnableVertexAttribArray(texAttribute());
}

void APIENTRY clientDisable(GLenum key)
{
    if (key == GL_VERTEX_ARRAY)
        glDisableVertexAttribArray(0);
    else if (key == GL_TEXTURE_COORD_ARRAY)
        glDisableVertexAttribArray(texAttribute());
}

void APIENTRY setAlpha(GLenum func, GLfloat ref)
{
    alphaFunc = func;
    alphaRef  = ref;
}

bool legacyCap(GLenum key)
{
    return key == GL_TEXTURE_2D || key == GL_FOG || key == GL_LIGHTING || key == GL_COLOR_MATERIAL || key == GL_NORMALIZE;
}

void APIENTRY setEnabled(GLenum key)
{
    if (key == GL_ALPHA_TEST)
        alphaTest = true;
    else if (!legacyCap(key))
        enable(key);
}

void APIENTRY setDisabled(GLenum key)
{
    if (key == GL_ALPHA_TEST)
        alphaTest = false;
    else if (!legacyCap(key))
        disable(key);
}

void APIENTRY setFog(GLenum key, GLfloat val)
{
    if (key == GL_FOG_START)
        fogStart = val;
    if (key == GL_FOG_END)
        fogEnd = val;
}

void APIENTRY setFogi(GLenum key, GLint val)
{
    setFog(key, float(val));
}

void APIENTRY setFogv(GLenum key, const GLfloat *v)
{
    if (key == GL_FOG_COLOR)
        std::copy(v, v + 4, fogColor);
    else
        setFog(key, *v);
}

void APIENTRY setHint(GLenum key, GLenum val)
{
    if (key != GL_FOG_HINT && key != GL_PERSPECTIVE_CORRECTION_HINT)
        hint(key, val);
}

void APIENTRY polygonMode(GLenum, GLenum)
{
}

// GLES always rasterizes filled triangles.
void APIENTRY texEnv(GLenum, GLenum, GLfloat)
{
}

// Polymost's shaders implement detail/glow modulation.
void APIENTRY depthRange(GLdouble a, GLdouble b)
{
    glDepthRangef(float(a), float(b));
}

void *APIENTRY mapBuffer(GLenum target, GLenum access)
{
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    GLbitfield flags = access == GL_READ_ONLY ? GL_MAP_READ_BIT : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT : GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;

    return glMapBufferRange(target, 0, size, flags);
}

void replace(std::string &s, const char *from, const char *to)
{
    size_t p = 0;

    while ((p = s.find(from, p)) != std::string::npos)
    {
        s.replace(p, strlen(from), to);
        p += strlen(to);
    }
}

void APIENTRY source(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    std::string s;
    for (int i = 0; i < count; i++)
        s.append(strings[i], lengths && lengths[i] >= 0 ? lengths[i] : strlen(strings[i]));

    // Only the engine's Polymost shaders need these explicit GLES attributes.
    if (s.find("gl_TexCoord") == std::string::npos && s.find("gl_ModelViewProjectionMatrix") == std::string::npos)
    {
        shaderSource(shader, count, strings, lengths);
        return;
    }

    GLint type;

    glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    auto version = s.find("#version");

    if (version != std::string::npos)
    {
        auto end = s.find('\n', version);
        s.erase(version, end == std::string::npos ? s.size() - version : end - version + 1);
    }

    replace(s, "#extension GL_ARB_shader_texture_lod : enable", "#define LIBRETRO_TEXTURE_LOD 1");
    replace(s, "#ifdef GL_ARB_shader_texture_lod", "#ifdef LIBRETRO_TEXTURE_LOD");
    replace(s, "gl_ModelViewProjectionMatrix", "lr_mvp");
    replace(s, "gl_ModelViewMatrix", "lr_model");
    replace(s, "gl_TextureMatrix", "lr_texture");
    replace(s, "gl_MultiTexCoord0", "lr_uv0");
    replace(s, "gl_MultiTexCoord3", "lr_uv3");
    replace(s, "gl_MultiTexCoord4", "lr_uv4");
    replace(s, "gl_Vertex", "lr_position");
    replace(s, "gl_Color", "lr_color");
    replace(s, "gl_TexCoord", "lr_texcoord");
    replace(s, "gl_FogFragCoord", "lr_fogcoord");
    replace(s, "gl_Fog.color", "lr_fogColor");
    replace(s, "gl_Fog.end", "lr_fogEnd");
    replace(s, "gl_Fog.scale", "lr_fogScale");
    replace(s, "gl_FragData[0]", "lr_fragColor");
    replace(s, "texture2DGradARB", "textureGrad");
    replace(s, "texture2D(", "texture(");
    std::string prefix = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

    if (type == GL_VERTEX_SHADER)
    {
        replace(s, "varying ", "out ");
        prefix += "layout(location=0) in vec4 lr_position; layout(location=1) in vec4 lr_uv0; layout(location=2) in vec4 lr_color; layout(location=3) in vec4 "
                  "lr_uv3; layout(location=4) in vec4 lr_uv4;\nuniform mat4 lr_model,lr_mvp,lr_texture[5]; out vec4 lr_texcoord[5]; out float lr_fogcoord;\n";
    }
    else
    {
        replace(s, "varying ", "in ");
        prefix += "in vec4 lr_texcoord[5]; in float lr_fogcoord; out vec4 lr_fragColor; uniform vec4 lr_fogColor; uniform float lr_fogEnd,lr_fogScale; uniform vec2 "
                  "lr_alpha;\n";

        // Alpha testing is a shader operation in GLES. Engine uses GREATER/GEQUAL.
        auto end = s.rfind('}');
        s.insert(end, "\nif (lr_alpha.x > 0.5 && (lr_alpha.x < 1.5 ? lr_fragColor.a <= lr_alpha.y : lr_fragColor.a < lr_alpha.y)) discard;\n");
    }

    s                = prefix + s;
    const char *text = s.c_str();
    shaderSource(shader, 1, &text, nullptr);
}

void APIENTRY compile(GLuint shader)
{
    compileShader(shader);
    GLint ok = 0;

    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok && logMessage)
    {
        char info[4096] = {};
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        logMessage(RETRO_LOG_ERROR, "GLES shader: %s\n", info);
    }
}

void APIENTRY link(GLuint program)
{
    linkProgram(program);
    uniforms.erase(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok && logMessage)
    {
        char info[4096] = {};
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        logMessage(RETRO_LOG_ERROR, "GLES program: %s\n", info);
    }
}

void upload()
{
    GLint program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (!program)
        return;

    auto it = uniforms.find(program);

    if (it == uniforms.end())
    {
        auto loc = [&](const char *n)
        {
            return glGetUniformLocation(program, n);
        };
        it = uniforms
             .emplace(program,
                      Uniforms { loc("lr_model"), loc("lr_mvp"), loc("lr_texture"), loc("lr_fogColor"), loc("lr_fogEnd"), loc("lr_fogScale"), loc("lr_alpha") })
             .first;
    }

    auto u = it->second;
    if (u.mvp < 0)
        return;

    auto mvp = multiply(projection.back(), model.back());

    glUniformMatrix4fv(u.model, 1, GL_FALSE, model.back().data());
    glUniformMatrix4fv(u.mvp, 1, GL_FALSE, mvp.data());
    float tex[80];
    for (int i = 0; i < 5; i++)
        std::copy(texture[i].back().begin(), texture[i].back().end(), tex + i * 16);

    glUniformMatrix4fv(u.tex, 5, GL_FALSE, tex);
    glUniform4fv(u.fogColor, 1, fogColor);
    glUniform1f(u.fogEnd, fogEnd);
    glUniform1f(u.fogScale, fogEnd == fogStart ? 0 : 1.f / (fogEnd - fogStart));
    glUniform2f(u.alpha, alphaTest ? (alphaFunc == GL_GEQUAL ? 2.f : 1.f) : 0.f, alphaRef);
}

void APIENTRY arrays(GLenum mode, GLint first, GLsizei count)
{
    upload();
    drawArrays(mode, first, count);
}

void APIENTRY elements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    upload();
    drawElements(mode, count, type, indices);
}

void APIENTRY begin(GLenum mode)
{
    primitive = mode;
    vertices.clear();
}

void APIENTRY uv(GLfloat s, GLfloat t)
{
    coords[0][0] = s;
    coords[0][1] = t;
    coords[0][2] = 0;
    coords[0][3] = 1;
}

void APIENTRY multiUv(GLenum unit, GLfloat s, GLfloat t)
{
    int i = unit - GL_TEXTURE0;
    if (i >= 0 && i < 5)
    {
        coords[i][0] = s;
        coords[i][1] = t;
        coords[i][2] = 0;
        coords[i][3] = 1;
    }
}

void APIENTRY vertex3(const GLfloat *p)
{
    Vertex v;
    std::copy(p, p + 3, v.pos);
    std::copy(color, color + 4, v.color);
    memcpy(v.uv, coords, sizeof(coords));
    vertices.push_back(v);
}

void APIENTRY vertex2(GLfloat x, GLfloat y)
{
    float p[] = { x, y, 0 };
    vertex3(p);
}

void APIENTRY vertex2i(GLint x, GLint y)
{
    vertex2(x, y);
}

void APIENTRY end()
{
    if (vertices.empty())
        return;

    if (primitive == GL_QUADS)
    {
        std::vector<Vertex> triangles;
        triangles.reserve(vertices.size() / 4 * 6);
        for (size_t i = 0; i + 3 < vertices.size(); i += 4)
            for (int j : { 0, 1, 2, 0, 2, 3 })
                triangles.push_back(vertices[i + j]);
        vertices.swap(triangles);
        primitive = GL_TRIANGLES;
    }

    if (primitive == GL_POLYGON)
        primitive = GL_TRIANGLE_FAN;

    GLint vao, buffer;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);

    if (!immediateVao)
        glGenVertexArrays(1, &immediateVao);

    if (!immediateBuffer)
        glGenBuffers(1, &immediateBuffer);

    glBindVertexArray(immediateVao);
    glBindBuffer(GL_ARRAY_BUFFER, immediateBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);

    const size_t offsets[] = { offsetof(Vertex, pos), offsetof(Vertex, uv), offsetof(Vertex, color), offsetof(Vertex, uv) + 3 * 4 * sizeof(float),
                               offsetof(Vertex, uv) + 4 * 4 * sizeof(float) };

    for (int i = 0; i < 5; i++)
    {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i == 0 ? 3 : 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void *)offsets[i]);
    }

    arrays(primitive, 0, vertices.size());
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
}
}  // namespace

void libretro_gles_init()
{
    model      = { identity() };
    projection = { identity() };
    for (auto &t : texture)
        t = { identity() };

    uniforms.clear();
    matrixMode    = GL_MODELVIEW;
    clientTexture = GL_TEXTURE0;
    alphaTest     = false;
    vertices.clear();
    enable       = glEnable;
    disable      = glDisable;
    getFloat     = glGetFloatv;
    shaderSource = glShaderSource;
    drawArrays   = glDrawArrays;
    drawElements = glDrawElements;
    hint         = glHint;

    glMatrixMode               = setMatrixMode;
    glLoadIdentity             = loadIdentity;
    glLoadMatrixf              = loadMatrix;
    glMultMatrixf              = multMatrix;
    glPushMatrix               = pushMatrix;
    glPopMatrix                = popMatrix;
    glTranslatef               = translate;
    glScalef                   = scale;
    glOrtho                    = ortho;
    glGetFloatv                = floatState;
    glColor4f                  = color4;
    glColor3f                  = color3;
    glColor4ub                 = colorBytes;
    glClientActiveTexture      = clientUnit;
    glVertexPointer            = vertexPointer;
    glTexCoordPointer          = texPointer;
    glEnableClientState        = clientEnable;
    glDisableClientState       = clientDisable;
    glEnable                   = setEnabled;
    glDisable                  = setDisabled;
    glAlphaFunc                = setAlpha;
    glFogf                     = setFog;
    glFogi                     = setFogi;
    glFogfv                    = setFogv;
    glHint                     = setHint;
    glPolygonMode              = polygonMode;
    glTexEnvf                  = texEnv;
    glDepthRange               = depthRange;
    glMapBuffer                = mapBuffer;
    glShaderSource             = source;
    glDrawArrays               = arrays;
    glDrawElements             = elements;
    glBegin                    = begin;
    glEnd                      = end;
    glTexCoord2f               = uv;
    glMultiTexCoord2f          = multiUv;
    glVertex2f                 = vertex2;
    glVertex2i                 = vertex2i;
    glVertex3fv                = vertex3;
    compileShader              = glCompileShader;
    linkProgram                = glLinkProgram;
    glCompileShader            = compile;
    glLinkProgram              = link;
    retro_log_callback logging = {};

    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        logMessage = logging.log;

    color4(1, 1, 1, 1);
}

void libretro_gles_destroy()
{
    if (immediateBuffer)
        glDeleteBuffers(1, &immediateBuffer);

    if (immediateVao)
        glDeleteVertexArrays(1, &immediateVao);

    immediateBuffer = immediateVao = 0;
    uniforms.clear();
    vertices.clear();
}
