#include "vr.h"

#include "core.h"
#include "duke3d.h"
#include "game.h"
#include "global.h"
#include "osd.h"
#include "player.h"
#include "renderlayer.h"
#include "vr_math.h"

LibretroVrMode libretro_vr_mode = LibretroVrMode::Off;

#ifdef USE_OPENGL

namespace
{
GLuint               textures[3] = {}, fbos[3] = {}, depth = 0, program = 0, buffer = 0, vao = 0;
int                  width = 0, height = 0, eyeIndex = -1;
unsigned             output = 0;
bool                 stereo = false, tracked = false, overlay = false, referenceValid = false;
float                reference[3] = {}, pitch = 0;
vec3_t               centerPosition;
int32_t              centerAngle;
int16_t              centerSector;
bool                 centerValid = false;
retro_vr_frame_state frame       = {};
LibretroVr::Crop     crops[2];
bool                 aimValid = false;
float                aimYaw = 0, aimPitch = 0, hudX = 0, hudY = 0;
int32_t              savedOffsetX = 0, savedOffsetY = 0;

void clearMap()
{
    retro_video_views map = {};
    environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &map);
}

bool publishMap()
{
    retro_video_view views[2] = { { 0, 0, unsigned(xdim / 2), unsigned(ydim), 0, RETRO_VIDEO_VIEW_EYE_LEFT, float(xdim) / ydim },
                                  { unsigned(xdim / 2), 0, unsigned(xdim / 2), unsigned(ydim), 0, RETRO_VIDEO_VIEW_EYE_RIGHT, float(xdim) / ydim } };

    retro_video_views map = {};
    map.views             = views;
    map.num_views         = 2;
    map.flags             = tracked ? RETRO_VIDEO_VIEWS_FLAG_TRACKED : 0;
    map.reference_space   = RETRO_VR_REFERENCE_SPACE_LOCAL;

    return environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &map);
}

GLuint shader(GLenum type, const char *source)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        glDeleteShader(s);
        return 0;
    }

    return s;
}

bool allocate()
{
    if (width == xdim && height == ydim && program)
        return true;

    libretro_vr_destroy();
#ifdef EDUKE32_GLES
    const char *vs = "#version 100\nattribute vec2 p; varying vec2 uv; void main(){uv=p*.5+.5;gl_Position=vec4(p,0.,1.);}";
    const char *fs = "#version 100\nprecision mediump float; varying vec2 uv; uniform sampler2D world,hud; uniform vec4 "
                     "crop; void main(){"
                     "vec2 c=crop.xy+uv*crop.zw;vec4 h=texture2D(hud,c);gl_FragColor=vec4(texture2D(world,c).rgb*(1.-h.a)+h.rgb,1.);}";
#else
    const char *vs = "#version 120\nattribute vec2 p; varying vec2 uv; void main(){uv=p*.5+.5;gl_Position=vec4(p,0.,1.);}";
    const char *fs = "#version 120\nvarying vec2 uv; uniform sampler2D world,hud; uniform vec4 crop; void main(){"
                     "vec2 c=crop.xy+uv*crop.zw;vec4 h=texture2D(hud,c);gl_FragColor=vec4(texture2D(world,c).rgb*(1.-h.a)+h.rgb,1.);}";
#endif
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);

    if (!v || !f)
    {
        if (v)
            glDeleteShader(v);
        if (f)
            glDeleteShader(f);

        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glBindAttribLocation(program, 0, "p");
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok)
    {
        libretro_vr_destroy();
        return false;
    }

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);

    const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenTextures(3, textures);
    glGenFramebuffers(3, fbos);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, xdim, ydim);

    for (int i = 0; i < 3; i++)
    {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xdim, ydim, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            libretro_vr_destroy();
            return false;
        }
    }

    width  = xdim;
    height = ydim;

    return true;
}

void beginTarget(int index)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
    polymost_resetState();
    glViewport(0, 0, xdim, ydim);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, index == 2 ? 0 : 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    polymost_forceShaderRebind();
}
}  // namespace

void libretro_vr_reset_tracking()
{
    referenceValid = stereo = tracked = aimValid = false;
    eyeIndex                                     = -1;
}

void libretro_vr_flat()
{
    clearMap();
}

void libretro_vr_destroy()
{
    glDeleteTextures(3, textures);
    glDeleteFramebuffers(3, fbos);

    if (depth)
        glDeleteRenderbuffers(1, &depth);

    if (program)
        glDeleteProgram(program);

    if (buffer)
        glDeleteBuffers(1, &buffer);

    if (vao)
        glDeleteVertexArrays(1, &vao);

    for (int i = 0; i < 3; i++)
        textures[i] = fbos[i] = 0;

    depth = program = buffer = vao = 0;
    width = height = 0;
    eyeIndex       = -1;
    overlay        = false;
}

void libretro_vr_frame(bool hardware, unsigned framebuffer)
{
    output = framebuffer;
    stereo = tracked = aimValid = false;
    eyeIndex                    = -1;

    unsigned status = 0;
    if (libretro_vr_mode == LibretroVrMode::Off || !hardware || !environ_cb(RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS, &status)
        || (status & (RETRO_VIDEO_VIEWS_STATUS_PRESENTS | RETRO_VIDEO_VIEWS_STATUS_STEREO)) != (RETRO_VIDEO_VIEWS_STATUS_PRESENTS | RETRO_VIDEO_VIEWS_STATUS_STEREO))
    {
        referenceValid = false;
        clearMap();
        return;
    }

    stereo = true;
    if (libretro_vr_mode == LibretroVrMode::Immersive && (status & RETRO_VIDEO_VIEWS_STATUS_HMD) && environ_cb(RETRO_ENVIRONMENT_GET_VR_FRAME_STATE, &frame)
        && LibretroVr::valid(frame.eyes[0]) && LibretroVr::valid(frame.eyes[1]))
    {
        tracked = true;
        if (!referenceValid || (frame.flags & RETRO_VR_FRAME_RECENTERED))
            for (int i = 0; i < 3; i++)
                reference[i] = (frame.eyes[0].position[i] + frame.eyes[1].position[i]) * .5f;
        referenceValid = true;

        retro_vr_controller_poses poses = {};
        if (environ_cb(RETRO_ENVIRONMENT_GET_VR_CONTROLLER_POSES, &poses))
        {
            const int order[2] = { RETRO_VR_HAND_RIGHT, RETRO_VR_HAND_LEFT };
            for (int hand : order)
            {
                auto &h = poses.hands[hand];
                if (!(h.flags & RETRO_VR_HAND_ACTIVE) || !(h.aim.flags & RETRO_VR_POSE_ORIENTATION_VALID))
                    continue;

                auto dir = LibretroVr::forward(h.aim.orientation);
                if (!std::isfinite(dir.x + dir.y + dir.z))
                    continue;

                aimYaw   = std::atan2(dir.x, -dir.z);
                aimPitch = std::asin(std::max(-1.f, std::min(1.f, dir.y)));

                // Direction of the aim ray relative to where the head looks, as a tangent on the HUD plane.
                auto  head = LibretroVr::unrotate(frame.eyes[0].orientation, dir);
                float depth = std::max(-head.z, .2f);
                float limit = 2.f;
                float tx = std::max(-limit, std::min(limit, head.x / depth));
                float ty = std::max(-limit, std::min(limit, head.y / depth));

                // 100 HUD units span a vertical half-extent of tangent/aspect.
                float scale = 100.f * (float(xdim) / ydim) / std::max(LibretroVr::crop(frame.eyes[0], float(xdim) / ydim).tangent, .05f);
                hudX        = tx * scale;
                hudY        = -ty * scale;
                aimValid    = true;
                break;
            }
        }
    }
    else
        referenceValid = false;
}

bool libretro_vr_draw(int player, int smoothratio)
{
    auto p = g_player[player].ps;
    if (!stereo || videoGetRenderMode() != REND_POLYMOST || g_screenCapture || !(p->gm & MODE_GAME) || (p->gm & MODE_MENU) || ud.overhead_on || ud.show_help
        || p->cursectnum < 0 || OSD_GetRowsCur() > 0 || OSD_IsMoving() || p->newowner >= 0 || p->over_shoulder_on || ud.camerasprite >= 0)
    {
        clearMap();
        return false;
    }

    if (!publishMap())
    {
        clearMap();
        return false;
    }

    GLint oldVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);

    if (!allocate())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, output);
        glBindVertexArray(oldVao);
        polymost_resetState();
        clearMap();
        return false;
    }

    glBindVertexArray(oldVao);
    int oldRange = viewingrange, oldAspect = yxaspect, oldHack = glprojectionhacks;
    int oldSize = ud.screen_size;
    // Full eye frustums; UI is rendered once separately below.
    vec2_t oldWindow1 = windowxy1, oldWindow2 = windowxy2;
    ud.screen_size = 0;

    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    if (tracked)
        glprojectionhacks = 0;

    for (int eye = 0; eye < 2; eye++)
    {
        eyeIndex   = eye;
        crops[eye] = tracked ? LibretroVr::crop(frame.eyes[eye], float(xdim) / ydim) : LibretroVr::Crop { 0, 0, 1, 1, 1 };
        beginTarget(eye);
        centerValid = false;
        G_DrawRooms(player, smoothratio);

        if (centerValid)
        {
            CAMERA(pos)    = centerPosition;
            CAMERA(q16ang) = centerAngle;
            CAMERA(sect)   = centerSector;
        }
    }

    eyeIndex          = -1;
    glprojectionhacks = oldHack;
    ud.screen_size    = oldSize;
    videoSetViewableArea(oldWindow1.x, oldWindow1.y, oldWindow2.x, oldWindow2.y);
    renderSetAspect(oldRange, oldAspect);
    renderSetRollAngle(0);
    beginTarget(2);
    overlay = true;
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    G_DisplayRest(smoothratio);
    overlay = false;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindFramebuffer(GL_FRAMEBUFFER, output);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(program);
    glBindVertexArray(vao);
    glUniform1i(glGetUniformLocation(program, "world"), 0);
    glUniform1i(glGetUniformLocation(program, "hud"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures[2]);

    for (int eye = 0; eye < 2; eye++)
    {
        auto c = crops[eye];
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[eye]);
        glUniform4f(glGetUniformLocation(program, "crop"), c.x, c.y, c.w, c.h);
        glViewport(eye * (xdim / 2), 0, xdim / 2, ydim);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray(oldVao);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, xdim, ydim);
    glClearColor(0, 0, 0, 1);
    polymost_resetState();
    polymost_forceShaderRebind();

    return true;
}

void libretro_vr_camera(int32_t *x, int32_t *y, int32_t *z, int32_t *angle, int16_t *sector)
{
    if (eyeIndex < 0)
        return;

    centerPosition = { *x, *y, *z };
    centerAngle    = *angle;
    centerSector   = *sector;
    centerValid    = true;

    float yaw   = fix16_to_float(*angle) * (LibretroVr::pi / 1024.f);
    float right = (eyeIndex == 0 ? -.032f : .032f), forward = 0, up = 0;

    if (tracked)
    {
        auto &eye = frame.eyes[eyeIndex];
        right     = eye.position[0] - reference[0];
        forward   = -(eye.position[2] - reference[2]);
        up        = eye.position[1] - reference[1];
        auto a    = LibretroVr::angles(eye.orientation);
        *angle += fix16_from_float(a.yaw * (1024.f / LibretroVr::pi));
        pitch = a.pitch;
        renderSetRollAngle(int(a.roll * (1024.f / LibretroVr::pi)));
        renderSetAspect(int(crops[eyeIndex].tangent * 65536.f), 81920);
    }

    vec3_t candidate = { *x + int((forward * std::cos(yaw) - right * std::sin(yaw)) * LibretroVr::unitsPerMeter),
                         *y + int((forward * std::sin(yaw) + right * std::cos(yaw)) * LibretroVr::unitsPerMeter), *z - int(up * LibretroVr::unitsPerMeter * 16) };

    int16_t sect = *sector;
    updatesectorz(candidate.x, candidate.y, candidate.z, &sect);

    if (sect >= 0)
    {
        *x      = candidate.x;
        *y      = candidate.y;
        *z      = candidate.z;
        *sector = sect;
    }
}

bool libretro_vr_angles(float *sine, float *cosine)
{
    if (eyeIndex < 0 || !tracked)
        return false;

    *sine   = -std::sin(pitch);
    *cosine = std::cos(pitch);

    return true;
}

bool libretro_vr_aim(int32_t *angleOffset, int32_t *horiz)
{
    if (!aimValid)
        return false;

    *angleOffset = int(aimYaw * (1024.f / LibretroVr::pi));
    *horiz       = 100 + int(std::tan(aimPitch) * 128.f);

    return true;
}

void libretro_vr_hud_begin()
{
    savedOffsetX = rotatesprite_x_offset;
    savedOffsetY = rotatesprite_y_offset;

    if (overlay && aimValid)
    {
        rotatesprite_x_offset += int(hudX * 65536.f);
        rotatesprite_y_offset += int(hudY * 65536.f);
    }
}

void libretro_vr_hud_end()
{
    rotatesprite_x_offset = savedOffsetX;
    rotatesprite_y_offset = savedOffsetY;
}

void libretro_vr_blend(unsigned source, unsigned dest)
{
    if (overlay)
        glBlendFuncSeparate(source, dest, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    else
        glBlendFunc(source, dest);
}

#else  // !USE_OPENGL

void libretro_vr_reset_tracking() {}
void libretro_vr_flat() {}
void libretro_vr_destroy() {}
void libretro_vr_frame(bool, unsigned) {}
bool libretro_vr_draw(int, int) { return false; }
void libretro_vr_camera(int32_t *, int32_t *, int32_t *, int32_t *, int16_t *) {}
bool libretro_vr_angles(float *, float *) { return false; }
bool libretro_vr_aim(int32_t *, int32_t *) { return false; }
void libretro_vr_hud_begin() {}
void libretro_vr_hud_end() {}
void libretro_vr_blend(unsigned, unsigned) {}

#endif
