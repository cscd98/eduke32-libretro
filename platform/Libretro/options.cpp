#include "options.h"

#include "core.h"
#include "global.h"
#include "player.h"
#include "retrolayer.h"
#include "vr.h"

#include <stdio.h>
#include <string.h>

// --------------------------------------------------------------------------
// Options state
// --------------------------------------------------------------------------
int  g_mouse_sensitivity = 4;
bool g_mouse_invert      = false;

// --------------------------------------------------------------------------
// Core options
// --------------------------------------------------------------------------
static retro_core_option_v2_category option_cats[] = {
    { "display", "Display", "Resolution and aspect ratio settings." },
    { "controller", "Controller", "Gameplay control settings." },
    { "mouse", "Mouse", "Mouse input settings." },
    { "player", "Player", "Player appearance settings." },
    { NULL, NULL, NULL },
};

static retro_core_option_v2_definition option_defs[] = {
    // ---- Display ----
    { "eduke32_resolution",
      "Display > Resolution",
      "Resolution",
      "Internal render resolution. Takes effect on next content load.",
      NULL,
      "display",
      {
      { "640x480", "640x480 (4:3)" },
      { "960x540", "960x540 (16:9)" },
      { "1280x720", "1280x720 (16:9)" },
      { "1600x900", "1600x900 (16:9)" },
      { "1920x1080", "1920x1080 (16:9)" },
      { NULL, NULL },
      },
      "1920x1080" },
    { "eduke32_aspect_ratio",
      "Display > Aspect Ratio",
      "Aspect Ratio",
      "Hint to the frontend for aspect ratio correction. "
      "Auto uses the ratio implied by the selected resolution.",
      NULL,
      "display",
      {
      { "auto", "Auto" },
      { "4:3", "4:3" },
      { "16:9", "16:9" },
      { "16:10", "16:10" },
      { NULL, NULL },
      },
      "auto" },
    { "eduke32_vr",
      "Display > VR Mode",
      "VR Mode",
      "Stereoscopic renders separate eyes on a stereo screen. "
      "Immersive uses headset tracking and per-eye fields of view. "
      "Requires frontend video-view support; otherwise uses normal rendering.",
      NULL,
      "display",
      { { "off", "Off" }, { "stereoscopic", "Stereoscopic" }, { "immersive", "Immersive" }, { NULL, NULL } },
      "off" },
    { "eduke32_vr_hud",
      "Display > VR HUD",
      "VR HUD",
      "Show the status bar, inventory and in-game messages in Immersive mode. The weapon and crosshair are always shown.",
      NULL,
      "display",
      { { "on", "On" }, { "off", "Off" }, { NULL, NULL } },
      "on" },
    { "eduke32_vr_hud_distance",
      "Display > VR HUD Distance",
      "VR HUD Distance",
      "How far away the HUD and weapon appear in Immersive mode. "
      "Closer makes them larger and may push the HUD out of view; farther makes them smaller.",
      NULL,
      "display",
      { { "2.0", "Closest" }, { "1.1", "Close" }, { "0.85", "Default" }, { "0.65", "Far" }, { "0.5", "Farthest" }, { NULL, NULL } },
      "0.85" },
    // ---- Controller ----
    { "eduke32_auto_aim",
      "Controller > Auto Aim",
      "Auto Aim",
      "Automatically aim weapons at nearby enemies.",
      NULL,
      "controller",
      {
      { "on", "On" },
      { "off", "Off" },
      { NULL, NULL },
      },
      "on" },
    { "eduke32_always_run",
      "Controller > Always Run",
      "Always Run",
      "Move at run speed without holding the run button.",
      NULL,
      "controller",
      {
      { "off", "Off" },
      { "on", "On" },
      { NULL, NULL },
      },
      "off" },
    // ---- Mouse ----
    { "eduke32_mouse_invert",
      "Mouse > Invert Y Axis",
      "Invert Y Axis",
      "Invert vertical mouse movement.",
      NULL,
      "mouse",
      {
      { "off", "Off" },
      { "on", "On" },
      { NULL, NULL },
      },
      "off" },
    { "eduke32_mouse_sensitivity",
      "Mouse > Sensitivity",
      "Sensitivity",
      "Multiplier applied to raw mouse movement deltas.",
      NULL,
      "mouse",
      {
      { "1", "1x" },
      { "2", "2x" },
      { "3", "3x" },
      { "4", "4x" },
      { "5", "5x" },
      { "6", "6x" },
      { "7", "7x" },
      { "8", "8x" },
      { NULL, NULL },
      },
      "4" },
    // ---- Player ----
    { "eduke32_player_color",
      "Player > Color",
      "Color",
      "Player color.",
      NULL,
      "player",
      {
      { "0", "Auto" },
      { "9", "Blue" },
      { "10", "Red" },
      { "11", "Green" },
      { "12", "Gray" },
      { "13", "Dark Gray" },
      { "14", "Dark Green" },
      { "15", "Brown" },
      { "16", "Dark Blue" },
      { "21", "Bright Red" },
      { "23", "Yellow" },
      { NULL, NULL },
      },
      "0" },
    { "eduke32_player_team",
      "Player > Team",
      "Team",
      "Player team (relevant in team-based multiplayer modes).",
      NULL,
      "player",
      {
      { "0", "Blue" },
      { "1", "Red" },
      { "2", "Green" },
      { "3", "Gray" },
      { NULL, NULL },
      },
      "0" },
    { NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL },
};

retro_core_options_v2 options_v2 = { option_cats, option_defs };

void apply_core_options(void)
{
    retro_variable var = {};
    var.key            = "eduke32_vr";
    libretro_vr_mode   = LibretroVrMode::Off;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (!strcmp(var.value, "stereoscopic"))
            libretro_vr_mode = LibretroVrMode::Stereoscopic;
        else if (!strcmp(var.value, "immersive"))
            libretro_vr_mode = LibretroVrMode::Immersive;
    }

    var.key                = "eduke32_vr_hud";
    libretro_vr_hud_enabled = true;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        libretro_vr_hud_enabled = strcmp(var.value, "off") != 0;

    var.key             = "eduke32_vr_hud_distance";
    libretro_vr_hud_tan = 0.85f;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        libretro_vr_hud_tan = (float)atof(var.value);

    // ---- Display: resolution ----
    var.key = "eduke32_resolution";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int w = 1920, h = 1080;
        sscanf(var.value, "%dx%d", &w, &h);
        ud.setup.xdim = w;
        ud.setup.ydim = h;

        if (game_initialised && (w != xdim || h != ydim))
        {
            videoSetGameMode(ud.setup.fullscreen, w, h, ud.setup.bpp, ud.detail);

            libretro_alloc_sw_frame(xdim, ydim);

            retro_game_geometry geom = {};
            geom.base_width          = (unsigned)xdim;
            geom.base_height         = (unsigned)ydim;
            geom.max_width           = (unsigned)RETRO_MAX_WIDTH;
            geom.max_height          = (unsigned)RETRO_MAX_HEIGHT;
            geom.aspect_ratio        = (float)xdim / (float)ydim;
            environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
        }
    }

    // ---- Display: aspect ratio ----
    var.key = "eduke32_aspect_ratio";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        float ar = 0.0f;  // 0 → auto: let the frontend derive it from base_width/base_height

        if (strcmp(var.value, "4:3") == 0)
            ar = 4.0f / 3.0f;
        else if (strcmp(var.value, "16:9") == 0)
            ar = 16.0f / 9.0f;
        else if (strcmp(var.value, "16:10") == 0)
            ar = 16.0f / 10.0f;

        retro_game_geometry geom = {};
        geom.base_width          = (unsigned)ud.setup.xdim;
        geom.base_height         = (unsigned)ud.setup.ydim;
        geom.max_width           = (unsigned)RETRO_MAX_WIDTH;
        geom.max_height          = (unsigned)RETRO_MAX_HEIGHT;
        geom.aspect_ratio        = ar;
        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
    }

    // ---- Controller: auto aim ----
    var.key = "eduke32_auto_aim";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        ud.config.AutoAim = (strcmp(var.value, "on") == 0) ? 1 : 0;

    // ---- Controller: always run ----
    var.key = "eduke32_always_run";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        ud.auto_run = (strcmp(var.value, "on") == 0) ? 1 : 0;

    // ---- Mouse ----
    var.key = "eduke32_mouse_invert";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        g_mouse_invert = (strcmp(var.value, "on") == 0);

    var.key = "eduke32_mouse_sensitivity";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        g_mouse_sensitivity = atoi(var.value);

    // ---- Player: color ----
    var.key = "eduke32_player_color";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        ud.color = atoi(var.value);
        if (game_initialised)
        {
            g_player[myconnectindex].pcolor       = ud.color;
            g_player[myconnectindex].ps->palookup = ud.color;
        }
    }

    // ---- Player: team ----
    var.key = "eduke32_player_team";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        ud.team = atoi(var.value);
        if (game_initialised)
            g_player[myconnectindex].pteam = ud.team;
    }
}
