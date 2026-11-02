#include "libretro.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// eDuke32 headers
// --------------------------------------------------------------------------
#include "anim.h"
#include "baselayer.h"
#include "build.h"
#include "common_game.h"
#include "control.h"
#include "duke3d.h"
#include "function.h"
#include "game.h"
#include "gamedef.h"
#include "global.h"
#include "keyboard.h"
#include "mmulti.h"
#include "osdcmds.h"
#include "renderlayer.h"
#include "sounds.h"

// --------------------------------------------------------------------------
// core headers
// --------------------------------------------------------------------------
#include "core.h"
#include "driver_libretro.h"
#include "gles.h"
#include "options.h"
#include "vr.h"

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------
static constexpr int        RETRO_BASE_WIDTH     = 640;
static constexpr int        RETRO_BASE_HEIGHT    = 480;
static float                retro_target_fps     = 60.0f;
static int                  audio_frames_per_run = 800;  // SAMPLE_RATE / fps
static unsigned             audio_buf_occupancy  = 50;   // 0-100, from status callback
double                      target_sample_rate   = DEFAULT_SAMPLE_RATE;
static constexpr int        AUDIO_SAMPLE_RATE    = DEFAULT_SAMPLE_RATE;
static std::vector<int16_t> audio_tmp;

// --------------------------------------------------------------------------
// RetroArch callbacks
// --------------------------------------------------------------------------
retro_environment_t               environ_cb     = nullptr;
retro_vfs_interface const        *vfs_iface      = nullptr;
static retro_video_refresh_t      video_cb       = nullptr;
static retro_audio_sample_t       audio_cb       = nullptr;
static retro_audio_sample_batch_t audio_batch_cb = nullptr;
static retro_input_poll_t         input_poll_cb  = nullptr;
static retro_input_state_t        input_state_cb = nullptr;
static retro_log_printf_t         log_cb         = nullptr;

// --------------------------------------------------------------------------
// HW render
// --------------------------------------------------------------------------
static retro_hw_render_callback hw_render           = {};
static bool                     use_hw              = false;
retro_hw_get_proc_address_t     get_proc_address_cb = nullptr;
static GLuint                   libretro_vao        = 0;
bool                            context_ready       = false;
bool                            g_logo_done         = false;

// --------------------------------------------------------------------------
// Input state
// --------------------------------------------------------------------------
struct ButtonMap
{
    unsigned retro_btn;
    int      gamefunc;
};

static const ButtonMap button_map[] = {
    { RETRO_DEVICE_ID_JOYPAD_UP, gamefunc_Move_Forward },
    { RETRO_DEVICE_ID_JOYPAD_DOWN, gamefunc_Move_Backward },
    { RETRO_DEVICE_ID_JOYPAD_LEFT, gamefunc_Turn_Left },
    { RETRO_DEVICE_ID_JOYPAD_RIGHT, gamefunc_Turn_Right },
    { RETRO_DEVICE_ID_JOYPAD_A, gamefunc_Run },
    { RETRO_DEVICE_ID_JOYPAD_B, gamefunc_Open },
    { RETRO_DEVICE_ID_JOYPAD_X, gamefunc_Jump },
    { RETRO_DEVICE_ID_JOYPAD_Y, gamefunc_Crouch },
    { RETRO_DEVICE_ID_JOYPAD_L, gamefunc_Previous_Weapon },
    { RETRO_DEVICE_ID_JOYPAD_R, gamefunc_Next_Weapon },
    { RETRO_DEVICE_ID_JOYPAD_L2, gamefunc_Alt_Fire },
    { RETRO_DEVICE_ID_JOYPAD_R2, gamefunc_Fire },
    { RETRO_DEVICE_ID_JOYPAD_L3, gamefunc_Map },
    { RETRO_DEVICE_ID_JOYPAD_R3, gamefunc_Toggle_Crosshair },
    { RETRO_DEVICE_ID_JOYPAD_SELECT, gamefunc_Inventory },
};

static constexpr int NUM_BUTTON_MAP = (int)(sizeof(button_map) / sizeof(button_map[0]));

static const struct
{
    unsigned retro_key;
    uint8_t  sc;
} keymap[] = {
    { RETROK_UP, sc_UpArrow },
    { RETROK_DOWN, sc_DownArrow },
    { RETROK_LEFT, sc_LeftArrow },
    { RETROK_RIGHT, sc_RightArrow },
    { RETROK_SPACE, sc_Space },
    { RETROK_RETURN, sc_Enter },
    { RETROK_ESCAPE, sc_Escape },
    { RETROK_LCTRL, sc_LeftControl },
    { RETROK_RCTRL, sc_RightControl },
    { RETROK_LALT, sc_LeftAlt },
    { RETROK_RALT, sc_RightAlt },
    { RETROK_LSHIFT, sc_LeftShift },
    { RETROK_RSHIFT, sc_RightShift },
    { RETROK_TAB, sc_Tab },
    { RETROK_BACKSPACE, sc_BackSpace },
    { RETROK_CAPSLOCK, sc_CapsLock },
    { RETROK_F1, sc_F1 },
    { RETROK_F2, sc_F2 },
    { RETROK_F3, sc_F3 },
    { RETROK_F4, sc_F4 },
    { RETROK_F5, sc_F5 },
    { RETROK_F6, sc_F6 },
    { RETROK_F7, sc_F7 },
    { RETROK_F8, sc_F8 },
    { RETROK_F9, sc_F9 },
    { RETROK_F10, sc_F10 },
    { RETROK_F11, sc_F11 },
    { RETROK_F12, sc_F12 },
    { RETROK_a, sc_A },
    { RETROK_b, sc_B },
    { RETROK_c, sc_C },
    { RETROK_d, sc_D },
    { RETROK_e, sc_E },
    { RETROK_f, sc_F },
    { RETROK_g, sc_G },
    { RETROK_h, sc_H },
    { RETROK_i, sc_I },
    { RETROK_j, sc_J },
    { RETROK_k, sc_K },
    { RETROK_l, sc_L },
    { RETROK_m, sc_M },
    { RETROK_n, sc_N },
    { RETROK_o, sc_O },
    { RETROK_p, sc_P },
    { RETROK_q, sc_Q },
    { RETROK_r, sc_R },
    { RETROK_s, sc_S },
    { RETROK_t, sc_T },
    { RETROK_u, sc_U },
    { RETROK_v, sc_V },
    { RETROK_w, sc_W },
    { RETROK_x, sc_X },
    { RETROK_y, sc_Y },
    { RETROK_z, sc_Z },
    { RETROK_0, sc_0 },
    { RETROK_1, sc_1 },
    { RETROK_2, sc_2 },
    { RETROK_3, sc_3 },
    { RETROK_4, sc_4 },
    { RETROK_5, sc_5 },
    { RETROK_6, sc_6 },
    { RETROK_7, sc_7 },
    { RETROK_8, sc_8 },
    { RETROK_9, sc_9 },
};

static constexpr int NUM_KEYS = (int)(sizeof(keymap) / sizeof(keymap[0]));

// ---------------------------------------------------------------------------
// Per-frame input cache.
// ---------------------------------------------------------------------------
static uint64_t s_input_frame   = 0;
static uint64_t s_applied_frame = ~0ULL;

// Cached raw state
static bool s_joypad_raw[16]     = {};
static bool s_keys_raw[NUM_KEYS] = {};

struct KeyRepeatState
{
    bool     held       = false;
    uint32_t nextRepeat = 0;

    bool update(bool down, uint32_t now)
    {
        if (!down)
        {
            held = false;
            return false;
        }

        if (!held)
        {
            held       = true;
            nextRepeat = now + 250;
            return true;
        }

        if ((int32_t)(now - nextRepeat) >= 0)
        {
            nextRepeat = now + 30;
            return true;
        }

        return false;
    }
};

static KeyRepeatState s_key_repeat[256];
static int32_t        s_mouse_dx = 0, s_mouse_dy = 0;
static bool           s_mb_l = false, s_mb_r = false, s_mb_m = false;

// Analog stick axes
float s_alx = 0.f, s_aly = 0.f, s_arx = 0.f, s_ary = 0.f;

static void radial_deadzone(float &x, float &y, float dz)
{
    float mag = sqrtf(x * x + y * y);

    if (mag < dz)
    {
        x = y = 0.f;
        return;
    }

    float scale = (fminf(mag, 1.f) - dz) / (1.f - dz) / mag;
    x *= scale;
    y *= scale;
}

// Drop the pad bit when its shadow key is down so keyboard play isn't doubled up.
static void mask_keyboard_shadowed_pad(void)
{
    static const struct { unsigned pad; unsigned key; } shadow[] = {
        { RETRO_DEVICE_ID_JOYPAD_B,      RETROK_z },
        { RETRO_DEVICE_ID_JOYPAD_A,      RETROK_x },
        { RETRO_DEVICE_ID_JOYPAD_Y,      RETROK_a },
        { RETRO_DEVICE_ID_JOYPAD_X,      RETROK_s },
        { RETRO_DEVICE_ID_JOYPAD_L,      RETROK_q },
        { RETRO_DEVICE_ID_JOYPAD_R,      RETROK_w },
        { RETRO_DEVICE_ID_JOYPAD_SELECT, RETROK_RSHIFT },
        { RETRO_DEVICE_ID_JOYPAD_START,  RETROK_RETURN },
    };

    for (auto const &s : shadow)
        if (s_joypad_raw[s.pad] && input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, s.key))
            s_joypad_raw[s.pad] = false;
}

void retro_poll_input_once(void)
{
    ++s_input_frame;
    input_poll_cb();

    for (int i = 0; i < 16; ++i)
        s_joypad_raw[i] = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i) != 0;

    s_alx = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32767.f;
    s_aly = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.f;
    s_arx = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.f;
    s_ary = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.f;

    radial_deadzone(s_alx, s_aly, 0.25f);
    radial_deadzone(s_arx, s_ary, 0.20f);

    for (int i = 0; i < NUM_KEYS; ++i)
        s_keys_raw[i] = input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keymap[i].retro_key) != 0;

    s_mouse_dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    s_mouse_dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    s_mb_l     = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
    s_mb_r     = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
    s_mb_m     = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) != 0;

    mask_keyboard_shadowed_pad();
}

void poll_input(void)
{
    // per-frame guard
    if (s_applied_frame == s_input_frame)
        return;

    s_applied_frame = s_input_frame;

    bool down[256] = {};
    for (int i = 0; i < NUM_KEYS; ++i)
        down[keymap[i].sc] |= s_keys_raw[i];

    bool const menu = logo_active || (g_player[myconnectindex].ps->gm & MODE_MENU);
    if (menu)
    {
        down[sc_UpArrow] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_UP] || s_aly < -0.5f;
        down[sc_DownArrow] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_DOWN] || s_aly > 0.5f;
        down[sc_LeftArrow] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_LEFT] || s_alx < -0.5f;
        down[sc_RightArrow] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_RIGHT] || s_alx > 0.5f;
        down[sc_Enter] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_B];   // Xbox A
        down[sc_Escape] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_A];  // Xbox B
    }
    else
    {
        for (auto const &mapping : button_map)
            for (auto key : ud.config.KeyboardKeys[mapping.gamefunc])
                if (key != 0xff && key != sc_None)
                    down[key] |= s_joypad_raw[mapping.retro_btn];
    }

    down[sc_Escape] |= s_joypad_raw[RETRO_DEVICE_ID_JOYPAD_START];

    uint32_t const now = timerGetTicks();
    for (unsigned sc = 0; sc < 256; ++sc)
    {
        bool const event = s_key_repeat[sc].update(down[sc], now);
        if (!down[sc])
            KB_KeyDown[sc] = 0;
        else if (event)
            KB_KeyDown[sc] = 1;
    }

    // -----------------------------------------------------------------------
    // Mouse → g_mousePos delta accumulator.
    // -----------------------------------------------------------------------
    extern int32_t g_mouseBits;

    g_mouseEnabled      = 1;
    g_mouseGrabbed      = 1;
    g_mouseInsideWindow = 1;

    g_mousePos.x += s_mouse_dx * g_mouse_sensitivity;
    g_mousePos.y += (g_mouse_invert ? -s_mouse_dy : s_mouse_dy) * g_mouse_sensitivity;

    g_mouseBits = (s_mb_l ? 1 : 0) | (s_mb_r ? 2 : 0) | (s_mb_m ? 4 : 0);

    // Right stick
    constexpr float STICK_LOOK_SPEED = 12.f;
    float           lx               = s_arx * fabsf(s_arx);
    float           ly               = s_ary * fabsf(s_ary);
    g_mousePos.x += (int32_t)(lx * STICK_LOOK_SPEED * g_mouse_sensitivity);
    g_mousePos.y += (int32_t)((g_mouse_invert ? -ly : ly) * STICK_LOOK_SPEED * g_mouse_sensitivity);
}

// ---------------------------------------------------------------------------
// Analog stick
// ---------------------------------------------------------------------------
static void libretro_dukeFillInputForTic(void)
{
    dukeFillInputForTic();  // runs CONTROL_GetInput first

    bool const    running = ud.runkey_mode ? (BUTTON(gamefunc_Run) | ud.auto_run) : (ud.auto_run ^ BUTTON(gamefunc_Run));
    int const     keyMove = running ? 80 : 40;  // NORMALKEYMOVE*2 / NORMALKEYMOVE
    constexpr int MAXVEL_ = 90;                 // same as player.cpp

    if (s_aly != 0.f)
    {
        int32_t fv      = localInput.fvel + (int32_t)lrintf(-s_aly * keyMove);
        localInput.fvel = (int16_t)max(-MAXVEL_, min(MAXVEL_, fv));
    }

    if (s_alx != 0.f)
    {
        // positive svel = strafe left
        int32_t sv      = localInput.svel - (int32_t)lrintf(s_alx * keyMove);
        localInput.svel = (int16_t)max(-MAXVEL_, min(MAXVEL_, sv));
    }
}

// --------------------------------------------------------------------------
// OpenGL context callbacks
// --------------------------------------------------------------------------
static void context_reset(void)
{
#ifdef USE_OPENGL
    context_ready = false;

    if (!libretro_glad_init() || !glGenVertexArrays || !glBindVertexArray || !glBindFramebuffer)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "Failed to init GL\n");
        return;
    }

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Graphics loader ready: %s\n", glGetString(GL_VERSION));

    fill_glinfo();

    glGenVertexArrays(1, &libretro_vao);
    glBindVertexArray(libretro_vao);

    GLuint fbo = hw_render.get_current_framebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (glDebugMessageCallback)
    {
        static unsigned debugMessages = 0;
        debugMessages                 = 0;
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
        [](GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *message, const void *)
        {
            if (log_cb && debugMessages++ < 32)
                log_cb(RETRO_LOG_WARN, "GL: %s\n", message);
        },
        nullptr);
    }

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Graphics context ready\n");
#endif

    context_ready = true;
}

static void context_destroy(void)
{
    libretro_vr_destroy();
#ifdef EDUKE32_GLES
    libretro_gles_destroy();
#endif
    libretro_vr_reset_tracking();
    context_ready = false;
}

// --------------------------------------------------------------------------
// Game-state
// --------------------------------------------------------------------------
bool               game_initialised  = false;
static bool        level_needs_enter = true;
static bool        level_loaded      = false;
static std::string content_path;

// --------------------------------------------------------------------------
// Audio handling
// --------------------------------------------------------------------------
static void push_audio(int frames)
{
    const size_t samples = (size_t)frames * 2;  // stereo
    if (audio_tmp.size() < samples)
        audio_tmp.resize(samples);

    CustomDrv_PCM_Service(audio_tmp.data(), (int)(samples * sizeof(int16_t)));
    audio_batch_cb(audio_tmp.data(), (unsigned)frames);
}

// --------------------------------------------------------------------------
//  libretro API implementation
// --------------------------------------------------------------------------
extern "C"
{
// Forward declarations
static void audio_buffer_status_cb(bool active, unsigned occupancy, bool underrun_likely);
void        main_loop_restart();

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    vfs_iface = nullptr;
    libretro_vfs_set_directory(nullptr);
    retro_vfs_interface_info vfs_info = { 5, nullptr };
    if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_info) && vfs_info.required_interface_version >= 5)
    {
        auto const *api = vfs_info.iface;
        if (api && api->open && api->close && api->read && api->write && api->seek && api->size && api->flush && api->stat_64 && api->mkdir && api->remove
            && api->opendir && api->readdir && api->closedir && api->dirent_get_name && api->dirent_is_dir)
            vfs_iface = api;
    }

    retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "libretro-eduke32: %s\n", vfs_iface ? "using frontend VFS v5" : "VFS v5 unavailable, using native filesystem");
}

RETRO_API void retro_init(void)
{
    const char *saveDirectory = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saveDirectory))
        libretro_vfs_set_directory(saveDirectory);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "XRGB8888 not supported\n");
    }

    double rate = DEFAULT_SAMPLE_RATE;

    /* Ask frontend for its preferred output rate */
    if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE, &rate) && rate > 0.0)
        target_sample_rate = rate;

    // Query frontend's target refresh rate to size audio buffers correctly.
    float fps = 0.0f;
    if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &fps) && fps > 0.0f)
        retro_target_fps = fps;
    audio_frames_per_run = (int)((float)target_sample_rate / retro_target_fps + 0.5f);

    // register occupancy cb
    retro_audio_buffer_status_callback audio_status = { audio_buffer_status_cb };
    environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &audio_status);

    // Register options v2
    if (!environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_v2))
        if (log_cb)
            log_cb(RETRO_LOG_WARN, "libretro-eduke32: options v2 not supported by frontend\n");

    // Read initial option values so ud.setup is populated before retro_load_game
    apply_core_options();

    // Controller description
    static const retro_controller_description controllers[] = {
        { "Dual Analog Gamepad", RETRO_DEVICE_JOYPAD },
        { nullptr, 0 },
    };

    static const retro_controller_info ports[] = {
        { controllers, 1 },
        { nullptr, 0 },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

    // Input descriptors
    static const retro_input_descriptor descs[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Move Forward" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Move Backward" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Turn Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Turn Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Run" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Open / Interact" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Jump" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Crouch" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Previous Weapon" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Next Weapon" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Alt Fire" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Fire" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Automap" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Inventory" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu / Pause" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Strafe (Analog)" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Move (Analog)" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Turn (Analog)" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Look (Analog)" },
        { 0, 0, 0, 0, nullptr },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descs);

    static const char *fake_argv[] = { "eduke32_libretro", nullptr };
    int                fake_argc   = 1;

    engineSetupAllocator();
    engineSetupLogging(fake_argc, const_cast<char **>(fake_argv));
    engineSetLogFile(APPBASENAME ".log", LOG_GAME_MAX);

    osdcallbacks_t callbacks = {};
    callbacks.drawchar       = dukeConsolePrintChar;
    callbacks.drawstr        = dukeConsolePrintString;
    callbacks.drawcursor     = dukeConsolePrintCursor;
    callbacks.getcolumnwidth = dukeConsoleGetColumnWidth;
    callbacks.getrowheight   = dukeConsoleGetRowHeight;
    callbacks.clear          = dukeConsoleClearBackground;
    callbacks.gettime        = BGetTime;
    callbacks.onshowosd      = dukeConsoleOnShowCallback;

    LOG_F(INFO, HEAD2 " %s", s_buildRev);
    PrintBuildInfo();

    g_maxDefinedSkill = 4;
    ud.multimode      = 1;

    G_MaybeAllocPlayer(0);

    hash_init(&h_gamefuncs);
    for (bssize_t i = NUMGAMEFUNCTIONS - 1; i >= 0; i--)
    {
        if (gamefunctions[i][0] == '\0')
            continue;
        hash_add(&h_gamefuncs, gamefunctions[i], i, 0);
    }

    CONFIG_ReadSetup();

    apply_core_options();
}

RETRO_API void retro_deinit(void)
{
    if (game_initialised)
    {
        S_SoundShutdown();
        S_MusicShutdown();
        engineUnInit();
        game_initialised = false;
    }
}

RETRO_API unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(retro_system_info *info)
{
    Bmemset(info, 0, sizeof(*info));
    info->library_name     = "EDuke32";
    info->library_version  = "1.0.0";
    info->valid_extensions = "grp|con|zip|pk3|pk4";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info *info)
{
    int const w = (ud.setup.xdim > 0) ? ud.setup.xdim : RETRO_BASE_WIDTH;
    int const h = (ud.setup.ydim > 0) ? ud.setup.ydim : RETRO_BASE_HEIGHT;

    info->geometry.base_width   = (unsigned)w;
    info->geometry.base_height  = (unsigned)h;
    info->geometry.max_width    = (unsigned)RETRO_MAX_WIDTH;
    info->geometry.max_height   = (unsigned)RETRO_MAX_HEIGHT;
    info->geometry.aspect_ratio = (float)w / (float)h;
    info->timing.fps            = retro_target_fps;
    info->timing.sample_rate    = target_sample_rate;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

RETRO_API void retro_reset(void)
{
    g_player[myconnectindex].ps->gm = MODE_RESTART;
}

bool init_game()
{
    if (enginePreInit())
        log_cb(RETRO_LOG_INFO, "libretro-eduke32: engine failed to preinit\n");
    {
        std::string dir     = content_path;
        auto        slash   = dir.find_last_of("/\\");
        std::string grpfile = (slash != std::string::npos) ? dir.substr(slash + 1) : dir;
        if (slash != std::string::npos)
            dir = dir.substr(0, slash);
        else
            dir = ".";

        addsearchpath(dir.c_str());
        const char *grpPath = strstr(content_path.c_str(), "://") ? content_path.c_str() : grpfile.c_str();
        if (initgroupfile(grpPath) == -1)
        {
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "libretro-eduke32: could not open GRP '%s'\n", content_path.c_str());
            return false;
        }
    }

    G_ScanGroups();
    G_LoadGroups(!g_noAutoLoad && !ud.setup.noautoload);

    numplayers              = 1;
    g_mostConcurrentPlayers = ud.multimode;
    connectpoint2[0]        = -1;

    for (int i = 0; i < MAXPLAYERS; i++)
        G_MaybeAllocPlayer(i);

    G_Startup();

    g_player[0].playerquitflag = 1;

    auto &myplayer   = *g_player[myconnectindex].ps;
    myplayer.palette = BASEPAL;

    for (int i = 1, j = numplayers; j < ud.multimode; j++)
    {
        Bsprintf(g_player[j].user_name, "PLAYER %d", j + 1);
        g_player[j].ps->team = g_player[j].pteam = i;
        g_player[j].ps->weaponswitch             = 3;
        g_player[j].ps->auto_aim                 = 0;
        i                                        = 1 - i;
    }

    Anim_Init();

    char const *deffile = G_DefFile();
    if (!loaddefinitionsfile(deffile))
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "libretro-eduke32: loaded DEF file '%s'\n", deffile);
    loaddefinitions_game(deffile, FALSE);

    for (char *m : g_defModules)
        Xfree(m);
    g_defModules.clear();

    cacheAllSounds();

    if (enginePostInit())
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "libretro-eduke32: enginePostInit() failed\n");
        return false;
    }

    G_PostLoadPalette();
    tileDelete(MIRROR);
    Gv_ResetSystemDefaults();

    ud.m_level_number  = 0;
    ud.m_volume_number = 0;
    ud.warp_on         = 0;

    g_player[0].ps->palette = BASEPAL;

    connecthead             = 0;
    connectpoint2[0]        = -1;
    myconnectindex          = 0;
    g_mostConcurrentPlayers = ud.multimode;

    ++ud.executions;

    char const *rtsname = g_rtsNamePtr ? g_rtsNamePtr : ud.rtsname;
    RTS_Init(rtsname);

    ud.last_level = -1;

    Bsprintf(tempbuf, HEAD2 " %s", s_buildRev);
    OSD_SetVersion(tempbuf, 10, 0);
    OSD_SetParameters(0, 0, 0, 12, 2, 12, OSD_ERROR, OSDTEXT_RED, OSDTEXT_DARKRED, gamefunctions[gamefunc_Show_Console][0] == '\0' ? OSD_PROTECTED : 0);
    registerosdcommands();

    extern int32_t BGetTime(void);
    CONTROL_Startup(controltype_keyboardandmouse, &BGetTime, TICRATE);

    G_SetupGameButtons();
    CONFIG_SetupMouse();
    CONFIG_SetupJoystick();

    CONTROL_JoystickEnabled = (ud.setup.usejoystick && CONTROL_JoyPresent);
    CONTROL_MouseEnabled    = ud.setup.usemouse;

    CONFIG_ReadSettings();

    OSD_Exec("autoexec.cfg");
    CONFIG_SetDefaultKeys(keydefaults, true);
    system_getcvars();

    if (videoSetGameMode(ud.setup.fullscreen, ud.setup.xdim, ud.setup.ydim, ud.setup.bpp, ud.detail) < 0)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "Failure setting video mode %dx%dx%d %s! Trying next mode...", ud.setup.xdim, ud.setup.ydim, ud.setup.bpp,
                   ud.setup.fullscreen ? "fullscreen" : "windowed");
    }
#ifdef USE_OPENGL
    polymost_debugShaders();
#endif

    retro_game_geometry geom = {};
    geom.base_width          = (unsigned)xdim;
    geom.base_height         = (unsigned)ydim;
    geom.max_width           = (unsigned)RETRO_MAX_WIDTH;
    geom.max_height          = (unsigned)RETRO_MAX_HEIGHT;
    geom.aspect_ratio        = (float)xdim / (float)ydim;
    environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);

    videoSetPalette(ud.brightness >> 2, myplayer.palette, 0);

    ud.config.MusicDevice = ASS_OPL3;
    S_SoundStartup();
    S_MusicStartup();
    G_InitText();
    Menu_Init();
    ReadSaveGameHeaders();
    FX_StopAllSounds();
    S_ClearSoundLocks();

    VM_OnEvent(EVENT_INITCOMPLETE);

    libretro_alloc_sw_frame(xdim, ydim);

    main_loop_restart();
    game_initialised = true;

    return true;
}

void main_loop_restart()
{
    totalclock  = 0;
    ototalclock = 0;
    lockclock   = 0;

    auto &myplayer = *g_player[myconnectindex].ps;
    myplayer.fta   = 0;
    for (int32_t &q : user_quote_time)
        q = 0;

    Menu_Change(MENU_MAIN);

    if (myplayer.gm & MODE_NEWGAME)
    {
        G_NewGame(ud.m_volume_number, ud.m_level_number, ud.m_player_skill);
        myplayer.gm = MODE_RESTART;
    }
    else
    {
        if (ud.warp_on == 1)
            G_NewGame_EnterLevel();

        if (ud.warp_on == 0)
        {
            if ((g_netServer || ud.multimode > 1) && boardfilename[0] != 0)
            {
                ud.m_level_number     = 7;
                ud.m_volume_number    = 0;
                ud.m_respawn_monsters = !!(ud.m_player_skill == 4);

                for (int TRAVERSE_CONNECT(i))
                {
                    P_ResetWeapons(i);
                    P_ResetInventory(i);
                }

                G_NewGame_EnterLevel();
                Net_WaitForServer();
            }
            else if (g_networkMode != NET_DEDICATED_SERVER)
            {
                if (!g_logo_done)
                    G_DisplayLogo_Start();
                if (!logo_active)
                    G_BackToMenu();
            }

            /*if (g_networkMode != NET_DEDICATED_SERVER)
        {
            if (G_PlaybackDemo())
            {
                FX_StopAllSounds();
                g_noLogoAnim = 1;
                main_loop_restart();
            }
        }*/
        }
        else
            G_UpdateScreenArea();
    }

    ud.showweapons = ud.config.ShowWeapons;
    P_SetupMiscInputSettings();
    g_player[myconnectindex].pteam = ud.team;

    if (g_gametypeFlags[ud.coop] & GAMETYPE_TDM)
        myplayer.palookup = g_player[myconnectindex].pcolor = G_GetTeamPalette(g_player[myconnectindex].pteam);
    else if (ud.color)
        myplayer.palookup = g_player[myconnectindex].pcolor = ud.color;
    else
        myplayer.palookup = g_player[myconnectindex].pcolor;

    ud.warp_on           = 0;
    KB_KeyDown[sc_Pause] = 0;
}

// --------------------------------------------------------------------------
// retro_load_game
// --------------------------------------------------------------------------
RETRO_API bool retro_load_game(const retro_game_info *info)
{
    if (!info || !info->path)
        return false;

    content_path = info->path;

#ifdef USE_OPENGL
    hw_render.context_type  = RETRO_HW_CONTEXT_OPENGL;
    hw_render.version_major = 3;
#if defined EDUKE32_GLES
    hw_render.context_type  = RETRO_HW_CONTEXT_OPENGLES3;
    hw_render.version_major = 3;
#endif
#else
    hw_render.context_type = RETRO_HW_CONTEXT_NONE;
#endif
    hw_render.version_minor      = 0;
    hw_render.context_reset      = context_reset;
    hw_render.context_destroy    = context_destroy;
    hw_render.depth              = true;
    hw_render.stencil            = false;
    hw_render.bottom_left_origin = true;

#ifdef USE_OPENGL
#ifndef EDUKE32_GLES
    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, nullptr) && log_cb)
        log_cb(RETRO_LOG_WARN, "Frontend does not support shared OpenGL contexts; menu rendering may be affected\n");
#endif
    if (environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
    {
        use_hw              = true;
        get_proc_address_cb = hw_render.get_proc_address;
        nogl                = 0;
    }
    else
#endif
    {
        hw_render.context_type = RETRO_HW_CONTEXT_NONE;
        context_ready          = true;
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "Using software rendering\n");
    }

    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t)
{
    return false;
}

RETRO_API void retro_unload_game(void)
{
    libretro_vr_reset_tracking();
    if (game_initialised)
    {
        S_SoundShutdown();
        S_MusicShutdown();
        engineUnInit();
        FreeGroups();
        game_initialised = false;
    }
}

RETRO_API unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

RETRO_API void retro_run(void)
{
    // Re-apply options whenever the user changes them in the Quick Menu
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        apply_core_options();

    if (!context_ready)
    {
        video_cb(NULL, xdim, ydim, 0);
        return;
    }

#ifdef USE_OPENGL
    if (use_hw)
    {
        glBindVertexArray(libretro_vao);
        glBindFramebuffer(GL_FRAMEBUFFER, hw_render.get_current_framebuffer());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifndef EDUKE32_GLES
        for (GLenum unit : { GL_TEXTURE0, GL_TEXTURE3, GL_TEXTURE4 })
        {
            glActiveTexture(unit);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
        }

        glActiveTexture(GL_TEXTURE0);
        glClientActiveTexture(GL_TEXTURE0);
        glMatrixMode(GL_MODELVIEW);
#endif

        if (game_initialised)
            polymost_resetState();
    }
#endif

    if (!game_initialised)
        init_game();

    libretro_vr_frame(use_hw, use_hw ? hw_render.get_current_framebuffer() : 0);

    auto &myplayer = *g_player[myconnectindex].ps;

    retro_poll_input_once();

    Net_GetPackets();
    handleevents();

    const int adjust  = ((int)audio_buf_occupancy - 50) * audio_frames_per_run / 200;
    const int frames  = audio_frames_per_run - adjust;  // more frames when underrunning
    const int clamped = frames < audio_frames_per_run * 3 / 4   ? audio_frames_per_run * 3 / 4
                        : frames > audio_frames_per_run * 5 / 4 ? audio_frames_per_run * 5 / 4
                                                                : frames;

    if (logo_active)
    {
        libretro_vr_flat();
        MUSIC_Update();

        if (use_hw)
        {
            GLuint fbo = hw_render.get_current_framebuffer();
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, xdim, ydim);
            polymost_forceShaderRebind();
        }

        bool done = G_DisplayLogo_Frame();

        if (!use_hw)
            videoNextPage();

        if (use_hw)
            video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)xdim, (unsigned)ydim, 0);
        else
            video_cb(libretro_sw_frame, xdim, ydim, libretro_sw_pitch);

        push_audio(clamped);

        if (done)
        {
            g_logo_done = true;
            G_BackToMenu();
        }
        return;
    }

    if (((myplayer.gm & (MODE_MENU | MODE_DEMO)) == 0) && (int32_t)(totalclock - ototalclock) >= TICSPERFRAME)
    {
        do
        {
            if (g_frameJustDrawn && (myplayer.gm & (MODE_MENU | MODE_DEMO)) == 0)
                libretro_dukeFillInputForTic();

            ototalclock += TICSPERFRAME;

            if (((ud.show_help == 0 && (myplayer.gm & MODE_MENU) != MODE_MENU) || ud.recstat == 2) && (myplayer.gm & MODE_GAME))
            {
                g_frameJustDrawn = false;
                Net_GetPackets();
                G_DoMoveThings();
            }
        } while ((myplayer.gm & (MODE_MENU | MODE_DEMO)) == 0 && (int32_t)(totalclock - ototalclock) >= TICSPERFRAME);
    }

    G_DoCheats();

    if (myplayer.gm & MODE_NEWGAME)
    {
        main_loop_restart();
        push_audio(clamped);
        return;
    }

    if (myplayer.gm & (MODE_EOL | MODE_RESTART))
    {
#ifdef USE_OPENGL
        polymost_resetShaderIDs();
#endif
        switch (G_EndOfLevel())
        {
            case 1:
                video_cb(NULL, (unsigned)xdim, (unsigned)ydim, 0);
                push_audio(clamped);
                return;
            case 2:
                main_loop_restart();
                break;
        }
    }

    if (use_hw)
    {
        GLuint fbo = hw_render.get_current_framebuffer();
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, xdim, ydim);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        polymost_forceShaderRebind();
    }

    if ((myplayer.gm & (MODE_GAME | MODE_DEMO)) == 0)
    {
        libretro_vr_flat();
        CONTROL_BindsEnabled = false;

        G_HandleLocalKeys();
        OSD_DispatchQueued();
        G_DrawBackground();
        M_DisplayMenus();
        videoNextPage();

        S_Update();
    }
    else
        drawframe_do();

    if (myplayer.gm & MODE_DEMO)
        main_loop_restart();

    if (use_hw)
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)xdim, (unsigned)ydim, 0);
    else
        video_cb(libretro_sw_frame, xdim, ydim, libretro_sw_pitch);

    push_audio(clamped);
}

// --------------------------------------------------------------------------
// Misc
// --------------------------------------------------------------------------
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

// --------------------------------------------------------------------------
// Save / load state
// --------------------------------------------------------------------------
RETRO_API size_t retro_serialize_size(void)
{
    return 16 * 1024 * 1024;
}

RETRO_API bool retro_serialize(void * /*data*/, size_t /*size*/)
{
    return false;
}

RETRO_API bool retro_unserialize(const void * /*data*/, size_t /*size*/)
{
    return false;
}

// --------------------------------------------------------------------------
// Misc stubs
// --------------------------------------------------------------------------
RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned, bool, const char *)
{
}

RETRO_API void *retro_get_memory_data(unsigned)
{
    return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned)
{
    return 0;
}
}  // extern "C"

// --------------------------------------------------------------------------
// Call backs
// --------------------------------------------------------------------------
static void audio_buffer_status_cb(bool active, unsigned occupancy, bool underrun_likely)
{
    audio_buf_occupancy = occupancy;
    (void)active;
    (void)underrun_likely;
}
