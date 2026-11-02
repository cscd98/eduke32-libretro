#pragma once

#include <retro_timers.h>
#include <setjmp.h>

extern jmp_buf retro_nextpage_jmp;
extern bool    retro_nextpage_jmp_set;
extern int32_t g_noAutoLoad;
extern int32_t g_noLogoAnim;
extern int32_t g_noLogo;

extern bool libretro_glad_init(void);
extern void poll_input(void);
extern void retro_poll_input_once(void);

// Analog stick values cached
extern float    s_alx, s_aly, s_arx, s_ary;
extern uint8_t *libretro_sw_frame;
extern uint32_t libretro_sw_pitch;
extern void     libretro_alloc_sw_frame(int w, int h);

extern void G_PostLoadPalette(void);
extern void G_Startup(void);
extern void polymost_debugShaders(void);
extern bool context_ready;
extern void polymost_forceShaderRebind(void);
extern void G_DisplayLogo(void);
extern int  G_EndOfLevel(void);
extern void I_ClearAllInput(void);
extern void polymost_resetShaderIDs(void);

extern void dukeConsolePrintChar(int x, int y, char ch, int shade, int pal);
extern void dukeConsolePrintString(int x, int y, const char *ch, int len, int shade, int pal);
extern void dukeConsolePrintCursor(int x, int y, int type, int32_t lastkeypress);
extern int  dukeConsoleGetColumnWidth(int w);
extern int  dukeConsoleGetRowHeight(int h);

extern void dukeConsoleClearBackground(int numcols, int numrows);
extern void dukeConsoleOnShowCallback(int shown);

extern int32_t G_PlaybackDemo(void);
extern void    dukeFillInputForTic(void);
extern void    G_SetupGameButtons(void);

extern int32_t maxrefreshfreq;
extern void    G_FadePalette(int32_t r, int32_t g, int32_t b, int32_t e);
extern void    gameDisplay3DRScreen();
extern void    gameDisplayTitleScreen(void);
extern int32_t I_GeneralTrigger(void);

static inline void idle(int msec = 1)
{
    retro_sleep(msec);
}

extern bool logo_active;

void G_DisplayLogo_Start(void);
bool G_DisplayLogo_Frame(void);
