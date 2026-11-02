#include "retrolayer.h"

#include "baselayer.h"
#include "build.h"
#include "driver_libretro.h"
#include "duke3d.h"
#include "game.h"
#include "gles.h"
#include "global.h"
#include "libretro.h"
#include "player.h"
#include "screens.h"

#include "softsurface.h"
#include "palette.h"

#include <setjmp.h>

extern uint8_t *mirrorBuffer;
extern char     inpreparemirror;

// Various stubs
#ifdef _WIN32
int windowsCheckAlreadyRunning(void)
{
    return 1;
}
int32_t windowsCheckForUpdates(char *)
{
    return 0;
}
#endif

#if defined(NETCODE_DISABLE) && (defined(_WIN32) || (defined(__APPLE__) && __MAC_OS_X_VERSION_MIN_REQUIRED < 101200))
#include <chrono>

extern "C" int enet_gettime(int, struct timespec *ts)
{
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ts->tv_sec    = ns / 1000000000;
    ts->tv_nsec   = ns % 1000000000;
    return 0;
}
#endif

int32_t        inputchecked = 0;
char           quitevent = 0, appactive = 1, novideo = 0;
static int32_t vsync_renderlayer;
int32_t        maxrefreshfreq = 0;
int32_t        xres = -1, yres = -1, bpp = 0, fullscreen = 0, bytesperline;
double         refreshfreq        = 59.0;
intptr_t       frameplace         = 0;
int32_t        lockcount          = 0;
char           modechange         = 1;
char           offscreenrendering = 0;
char           videomodereset     = 0;
int32_t        nofog              = 0;

#ifndef EDUKE32_GLES
static uint16_t sysgamma[3][256];
#endif
#ifdef USE_OPENGL
char nogl = 0;
#endif

jmp_buf retro_nextpage_jmp;
bool    retro_nextpage_jmp_set = false;

extern bool use_hw;

uint8_t      *libretro_sw_frame = nullptr;
extern GLuint drawpolyVertsID;

int32_t initsystem(void)
{
    frameplace = 0;
    lockcount  = 0;

    return 0;
}

void uninitsystem(void)
{
}

void mouseGrabInput(bool grab)
{
}

int32_t wm_msgbox(const char *name, const char *fmt, ...)
{
    return 0;
}

int32_t startwin_puts(const char *s)
{
    UNREFERENCED_PARAMETER(s);
    return 0;
}
bool startwin_isopen(void)
{
    return false;
}

int32_t handleevents(void)
{
    poll_input();
    CustomDrv_PCM_Pump();

    timerUpdateClock();
    return 0;
}

int32_t handleevents_peekkeys(void)
{
    return 0;
}

const char *joyGetName(int32_t what, int32_t num)
{
    UNREFERENCED_PARAMETER(what);
    UNREFERENCED_PARAMETER(num);
    return "";
}

void joyScanDevices()
{
}

int32_t initinput(void (*hotplugCallback)(void) /*= nullptr*/)
{
    UNREFERENCED_PARAMETER(hotplugCallback);
    return 0;
}

void mouseUninit(void)
{
}

void wm_setapptitle(const char *name)
{
    UNREFERENCED_PARAMETER(name);
}

void system_getcvars(void)
{
}

int32_t wm_ynbox(const char *name, const char *fmt, ...)
{
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(fmt);
    return 0;
}

void mouseLockToWindow(char a)
{
    UNREFERENCED_PARAMETER(a);
}

void mouseInit(void)
{
}

void uninitinput(void)
{
}


char const *videoGetDisplayName(int display)
{
    UNREFERENCED_PARAMETER(display);
    return "Primary display";
}

int32_t videoSetGamma(void)
{
    return 0;
}

int32_t videoUpdatePalette(int32_t start, int32_t num)
{
    UNREFERENCED_PARAMETER(start);
    UNREFERENCED_PARAMETER(num);
#ifndef USE_OPENGL
    if (bpp <= 8)
        softsurface_setPalette(curpalettefaded, 0x00FF0000, 0x0000FF00, 0x000000FF);
#endif
    return 0;
}

void videoBeginDrawing(void)
{
#ifdef USE_OPENGL
    lockcount++;
    if (glIsBuffer(drawpolyVertsID))
        glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);
#else
    if (bpp > 8)
        return;

    if (lockcount++ > 0)
        return;

    static intptr_t backupFrameplace = 0;

    if (inpreparemirror)
    {
        if (offscreenrendering)
        {
            if (!backupFrameplace)
                backupFrameplace = frameplace;
            else if (frameplace != (intptr_t)mirrorBuffer && frameplace != backupFrameplace)
                return;
        }

        frameplace = (intptr_t)mirrorBuffer;

        if (offscreenrendering)
            return;
    }
    else if (offscreenrendering)
    {
        if (backupFrameplace)
        {
            frameplace       = backupFrameplace;
            backupFrameplace = 0;
        }
        return;
    }
    else
        frameplace = (intptr_t)softsurface_getBuffer();

    if (modechange)
    {
        bytesperline = xdim;
        calc_ylookup(bytesperline, ydim);
        modechange = 0;
    }
#endif
}

void videoEndDrawing(void)
{
#ifdef USE_OPENGL
    if (lockcount > 0)
        lockcount--;
#else
    if (bpp > 8 || !frameplace)
        return;

    if (lockcount > 1)
    {
        lockcount--;
        return;
    }

    if (!offscreenrendering)
        frameplace = 0;
    lockcount = 0;
#endif
}

void videoSetPalette(int32_t, int32_t, int32_t)
{
}

void videoShowFrame(int32_t w)
{
    UNREFERENCED_PARAMETER(w);
#ifdef USE_OPENGL
    // Rebind VBO
    extern GLuint drawpolyVertsID;
    if (glIsBuffer(drawpolyVertsID))
        glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);
#else
    if (offscreenrendering)
        return;

    while (lockcount)
        videoEndDrawing();

    if (libretro_sw_frame && softsurface_getBuffer())
        softsurface_blitBuffer((uint32_t *)libretro_sw_frame, 32);
#endif
}

int32_t videoSetVsync(int32_t newSync)
{
    UNREFERENCED_PARAMETER(newSync);
    return 0;
}

void videoGetModes(int display)
{
    UNREFERENCED_PARAMETER(display);
}

int32_t videoSetMode(int32_t x, int32_t y, int32_t c, int32_t fs)
{
#ifdef USE_OPENGL
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(c);
    UNREFERENCED_PARAMETER(fs);
#else
    UNREFERENCED_PARAMETER(c);

    xres       = x;
    yres       = y;
    bpp        = 8;
    fullscreen = fs;
    modechange = 1;
    lockcount  = 0;
    frameplace = 0;

    softsurface_initialize({ x, y }, { x, y });
    videoUpdatePalette(0, 256);
#endif

    return 0;
}

uint32_t libretro_sw_pitch = 0;

void libretro_alloc_sw_frame(int w, int h)
{
    free(libretro_sw_frame);

#ifdef USE_OPENGL
    int bytes_per_pixel = (bpp > 8) ? 4 : 1;
#else
    int bytes_per_pixel = 4;
#endif

    libretro_sw_frame = (uint8_t *)calloc((size_t)w * h, bytes_per_pixel);
    libretro_sw_pitch = w * bytes_per_pixel;

#ifdef USE_OPENGL
    frameplace   = (intptr_t)libretro_sw_frame;
    bytesperline = w * bytes_per_pixel;
#endif
}

void videoResetMode(void)
{
}

#ifdef USE_OPENGL
#include "glad/glad.h"

extern retro_hw_get_proc_address_t get_proc_address_cb;

static void *libretro_get_proc_address(const char *name)
{
    return get_proc_address_cb ? (void *)get_proc_address_cb(name) : nullptr;
}

bool libretro_glad_init(void)
{
#ifdef EDUKE32_GLES
    if (!gladLoadGLES2Loader((GLADloadproc)libretro_get_proc_address))
        return false;

    if (GLVersion.major < 3)
        return false;

    libretro_gles_init();

    return true;
#else
    return gladLoadGLLoader((GLADloadproc)libretro_get_proc_address) != 0;
#endif
}
#endif

bool logo_active = false;

static bool     logo_did_playanim   = false;
static bool     logo_did_3dr        = false;
static bool     logo_did_title      = false;
static uint64_t logo_phase_start_ms = 0;
static bool     logo_phase_init     = false;
static int      logo_titlesound     = 0;

// Convert a wall-clock elapsed-ms value to BUILD totalclock ticks.
static inline int32_t logo_elapsed_ticks(void)
{
    if (!logo_phase_init)
        return 0;
    uint64_t elapsed_ms = (uint64_t)timerGetTicks() - logo_phase_start_ms;
    return (int32_t)(elapsed_ms * TICRATE / 1000);
}

void G_DisplayLogo_Start(void)
{
    if (logo_active)
        return;

    int32_t const logoflags = G_GetLogoFlags();

    if (g_noLogo || !(logoflags & LOGO_ENABLED) || VM_OnEventWithReturn(EVENT_MAINMENUSCREEN, g_player[myconnectindex].ps->i, myconnectindex, 0) != 0)
    {
        P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);
        logo_active = false;
        return;
    }

    logo_active         = true;
    logo_did_playanim   = false;
    logo_did_3dr        = false;
    logo_did_title      = false;
    logo_phase_init     = false;
    logo_phase_start_ms = 0;
    logo_titlesound     = 0;

    ready2send = 0;
    I_ClearAllInput();
    S_StopMusic();
    FX_StopAllSounds();
    S_ClearSoundLocks();

    ud.config.MusicToggle = 1;
    ud.config.MusicDevice = ASS_OPL3;

    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
    videoClearScreen(0L);
    renderFlushPerms();
    G_UpdateAppTitle();

    if (logoflags & LOGO_PLAYMUSIC)
        S_PlaySpecialMusicOrNothing(MUS_INTRO);
}

bool G_DisplayLogo_Frame(void)
{
    if (!logo_active)
        return true;

    int32_t const logoflags = G_GetLogoFlags();

    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    // ------------------------------------------------------------------
    // Phase: LOGO_PLAYANIM — Anim_Play is blocking, skip it
    // ------------------------------------------------------------------
    if (!logo_did_playanim)
    {
        logo_did_playanim = true;
        if (logoflags & LOGO_PLAYANIM)
        {
            if (logoflags & LOGO_STOPANIMSOUNDS)
            {
                FX_StopAllSounds();
                S_ClearSoundLocks();
            }
        }
        // fall through to 3DR
    }

    // ------------------------------------------------------------------
    // Phase: LOGO_3DRSCREEN
    // ------------------------------------------------------------------
    if (!logo_did_3dr && !NAM && (logoflags & LOGO_3DRSCREEN))
    {
        if (!logo_phase_init)
        {
            logo_phase_init     = true;
            logo_phase_start_ms = (uint64_t)timerGetTicks();
            I_ClearAllInput();
            P_SetGamePalette(g_player[myconnectindex].ps, DREALMSPAL, 8 + 2 + 1);
        }

        int32_t const ltc = logo_elapsed_ticks();

        if (g_restorePalette)
        {
            P_SetGamePalette(g_player[myconnectindex].ps, g_player[myconnectindex].ps->palette, 0);
            g_restorePalette = 0;
        }

        rotatesprite_fs(160 << 16, 100 << 16, 65536L, 0, DREALMS, 0, 0, 2 + 8 + 64 + BGSTRETCH);

        if (ltc >= 120 * 7)
        {
            I_ClearAllInput();
            logo_did_3dr        = true;
            logo_phase_init     = false;
            logo_phase_start_ms = 0;
        }

        return false;
    }

    logo_did_3dr = true;

    // ------------------------------------------------------------------
    // Phase: LOGO_TITLESCREEN
    // ------------------------------------------------------------------
    if (!logo_did_title && (logoflags & LOGO_TITLESCREEN))
    {
        if (!logo_phase_init)
        {
            logo_phase_init     = true;
            logo_titlesound     = 0;
            logo_phase_start_ms = (uint64_t)timerGetTicks();
            I_ClearAllInput();
            P_SetGamePalette(g_player[myconnectindex].ps, TITLEPAL, 8 + 2 + 1);
            renderFlushPerms();

            KB_FlushKeyboardQueue();
        }

        int32_t const ltc = logo_elapsed_ticks();

        rotatespritesmoothratio = calc_smoothratio(totalclock, ototalclock, REALGAMETICSPERSEC);

        rotatesprite_fs(160 << 16, 100 << 16, 65536L, 0, BETASCREEN, 0, 0, 2 + 8 + 64 + BGSTRETCH);

        if (logoflags & LOGO_DUKENUKEM)
        {
            if (ltc > 120 && ltc < (120 + 60))
            {
                if (logo_titlesound == 0)
                {
                    logo_titlesound++;
                    S_PlaySound(PIPEBOMB_EXPLODE);
                }

                rotatesprite_fs_id(160 << 16, 104 << 16, (ltc - 120) << 10, 0, DUKENUKEM, 0, 0, 2 + 8, W_DUKENUKEM);
            }
            else if (ltc >= (120 + 60))
                rotatesprite_fs_id(160 << 16, 104 << 16, 60 << 10, 0, DUKENUKEM, 0, 0, 2 + 8, W_DUKENUKEM);
        }
        else
            logo_titlesound = max(logo_titlesound, 1);

        if (logoflags & LOGO_THREEDEE)
        {
            if (ltc > 220 && ltc < (220 + 30))
            {
                if (logo_titlesound == 1)
                {
                    logo_titlesound++;
                    S_PlaySound(PIPEBOMB_EXPLODE);
                }

                if (logoflags & LOGO_DUKENUKEM)
                    rotatesprite_fs_id(160 << 16, 104 << 16, 60 << 10, 0, DUKENUKEM, 0, 0, 2 + 8, W_DUKENUKEM);

                rotatesprite_fs_id(160 << 16, 129 << 16, (ltc - 220) << 11, 0, THREEDEE, 0, 0, 2 + 8, W_THREEDEE);
            }
            else if (ltc >= (220 + 30))
                rotatesprite_fs_id(160 << 16, 129 << 16, 30 << 11, 0, THREEDEE, 0, 0, 2 + 8, W_THREEDEE);
        }
        else
            logo_titlesound = max(logo_titlesound, 2);

        if (PLUTOPAK && (logoflags & LOGO_PLUTOPAKSPRITE))
        {
            if (ltc >= 280 && ltc < 395)
            {
                if (logo_titlesound == 2)
                {
                    logo_titlesound++;
                    S_PlaySound(FLY_BY);
                }

                rotatesprite_fs_id(160 << 16, 151 << 16, (410 - ltc) << 12, 0, PLUTOPAKSPRITE + 1, (sintable[((int32_t)totalclock << 4) & 2047] >> 11), 0, 2 + 8,
                                   W_PLUTOPAK);
            }
            else if (ltc >= 395)
            {
                if (logo_titlesound == 3)
                {
                    logo_titlesound++;
                    S_PlaySound(PIPEBOMB_EXPLODE);
                }
                rotatesprite_fs_id(160 << 16, 151 << 16, 30 << 11, 0, PLUTOPAKSPRITE + 1, (sintable[((int32_t)totalclock << 4) & 2047] >> 11), 0, 2 + 8, W_PLUTOPAK);
            }
        }

        VM_OnEvent(EVENT_LOGO, -1, screenpeek);

        if (g_restorePalette)
        {
            P_SetGamePalette(g_player[myconnectindex].ps, g_player[myconnectindex].ps->palette, 0);
            g_restorePalette = 0;
        }

        if (ltc >= (860 + 120))
        {
            I_ClearAllInput();
            logo_did_title      = true;
            logo_phase_init     = false;
            logo_phase_start_ms = 0;
        }

        return false;
    }

    logo_did_title = true;

    // ------------------------------------------------------------------
    // All phases done
    // ------------------------------------------------------------------
    renderFlushPerms();
    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 0);
    if ((logoflags & LOGO_STOPMISCSOUNDS) == 0)
        S_PlaySound(NITEVISION_ONOFF);

    logo_active = false;

    return true;
}
