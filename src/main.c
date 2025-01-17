/*
 * OpenTyrian Classic: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "config.h"
#include "destruct.h"
#include "editship.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "helptext.h"
#include "hg_revision.h"
#include "joystick.h"
#include "jukebox.h"
#include "keyboard.h"
#include "loudness.h"
#include "mainint.h"
#include "mtrand.h"
#include "musmast.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"
#include "picload.h"
#include "scroller.h"
#include "setup.h"
#include "sprite.h"
#include "tyrian2.h"
#include "xmas.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"
#include "video_scale.h"

#include "SDL.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __BLACKBERRY__
#include "bbami.h"
#if 0
#include <eula.h>
#endif
#endif
const char *opentyrian_str = "OpenTyrian";
char opentyrian_version[100];
const char *opentyrian_menu_items[] =
{
    "About OpenTyrian",
#ifdef __BLACKBERRY__
#else
    "Toggle Fullscreen",
    "Scaler: 3x",
#endif
    /* "Play Destruct", */
    "Jukebox",
    "Return to Main Menu"
};

/* zero-terminated strncpy */
char *strnztcpy( char *to, const char *from, size_t count )
{
    to[count] = '\0';
    return strncpy(to, from, count);
}

void opentyrian_menu( void )
{
    int sel = 0;
    const int maxSel = COUNTOF(opentyrian_menu_items) - 1;
    bool quit = false, fade_in = true;

    uint temp_scaler = scaler;

    fade_black(10);
    JE_loadPic(VGAScreen, 13, false);

    draw_font_hv(VGAScreen, VGAScreen->w / 2, 5, opentyrian_str, large_font, centered, 15, -3);

    memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

    JE_showVGA();

    play_song(36); // A Field for Mag

    do
    {
        memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);

        for (int i = 0; i <= maxSel; i++)
        {
            const char *text = opentyrian_menu_items[i];
            char buffer[100];
            draw_font_hv_shadow(VGAScreen, VGAScreen->w / 2, (i != maxSel) ? i * 16 + 32 : 118, text, normal_font, centered, 15, (i != sel) ? -4 : -2, false, 2);
        }

        JE_showVGA();

        if (fade_in)
        {
            fade_in = false;
            fade_palette(colors, 20, 0, 255);
            wait_noinput(true, false, false);
        }

        tempW = 0;
        JE_textMenuWait(&tempW, false);

        if (newkey)
        {
            switch (lastkey_sym)
            {
                case SDLK_UP:
                    sel--;
                    if (sel < 0)
                    {
                        sel = maxSel;
                    }
                    JE_playSampleNum(S_CURSOR);
                    break;
                case SDLK_DOWN:
                    sel++;
                    if (sel > maxSel)
                    {
                        sel = 0;
                    }
                    JE_playSampleNum(S_CURSOR);
                    break;
                case SDLK_LEFT:
#ifdef __BLACKBERRY__
#else
                    if (sel == 2)
                    {
                        do
                        {
                            if (temp_scaler == 0)
                                temp_scaler = scalers_count;
                            temp_scaler--;
                        }
                        while (!can_init_scaler(temp_scaler, fullscreen_enabled));
                        JE_playSampleNum(S_CURSOR);
                    }
#endif
                    break;
                case SDLK_RIGHT:
#ifdef __BLACKBERRY__
#else
                    if (sel == 2)
                    {
                        do
                        {
                            temp_scaler++;
                            if (temp_scaler == scalers_count)
                                temp_scaler = 0;
                        }
                        while (!can_init_scaler(temp_scaler, fullscreen_enabled));
                        JE_playSampleNum(S_CURSOR);
                    }
#endif
                    break;
                case SDLK_RETURN:
                    switch (sel)
                    {
                        case 0: /* About */
                            JE_playSampleNum(S_SELECT);

                            scroller_sine(about_text);

                            memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
                            JE_showVGA();
                            fade_in = true;
                            break;
#ifdef __BLACKBERRY__
                        case 1: /* Jukebox */
#else
                        case 1: /* Fullscreen */
                            JE_playSampleNum(S_SELECT);

                            if (!init_scaler(scaler, !fullscreen_enabled) && // try new fullscreen state
                                !init_any_scaler(!fullscreen_enabled) &&     // try any scaler in new fullscreen state
                                !init_scaler(scaler, fullscreen_enabled))    // revert on fail
                            {
                                exit(EXIT_FAILURE);
                            }
                            set_palette(colors, 0, 255); // for switching between 8 bpp scalers
                            break;
                        case 2: /* Scaler */
                            JE_playSampleNum(S_SELECT);

                            if (scaler != temp_scaler)
                            {
                                if (!init_scaler(temp_scaler, fullscreen_enabled) &&   // try new scaler
                                    !init_scaler(temp_scaler, !fullscreen_enabled) &&  // try other fullscreen state
                                    !init_scaler(scaler, fullscreen_enabled))          // revert on fail
                                {
                                    exit(EXIT_FAILURE);
                                }
                                set_palette(colors, 0, 255); // for switching between 8 bpp scalers
                            }
                            break;
                        case 3: /* Jukebox */
#endif
                            JE_playSampleNum(S_SELECT);

                            fade_black(10);
                            jukebox();

                            memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
                            JE_showVGA();
                            fade_in = true;
                            break;
                        default: /* Return to main menu */
                            quit = true;
                            JE_playSampleNum(S_SPRING);
                            break;
                    }
                    break;
                case SDLK_ESCAPE:
                    quit = true;
                    JE_playSampleNum(S_SPRING);
                    break;
                default:
                    break;
            }
        }
    } while (!quit);
}

int main( int argc, char *argv[] )
{
    fprintf(stderr, "entered main\n");
    mt_srand(time(NULL));
#ifdef __BLACKBERRY__
    snprintf(opentyrian_version, sizeof(opentyrian_version), "Port for BlackBerry 10");

    bbami_info_ptr info;
    int rc = bbami_init(BBAMI_API_VERSION, DEFAULT_MANIFEST_PATH, &info);
    if(rc == 0) {
        int count;
        char * version;
        rc = bbami_query(info, BBAMI_APPLICATION_VERSION, &version);
        if(rc == 0) {
            snprintf(opentyrian_version, sizeof(opentyrian_version), "Port for BlackBerry 10, version %s", version);
            free(version);
        }
        bbami_done(info);
    }
#endif
    fprintf(stderr, "preparing SDL\n");
    if (SDL_Init(0)) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return -1;
    }

    fprintf(stderr, "loading configuration\n");

    JE_loadConfiguration();
    xmas = xmas_time();  // arg handler may override
    JE_paramCheck(argc, argv);

    JE_scanForEpisodes();

    fprintf(stderr, "initializing video\n");

    init_video();

    fprintf(stderr, "initializing keyboard\n");
    init_keyboard();
    init_joysticks();

    if (xmas && (!dir_file_exists(data_dir(), "tyrianc.shp") || !dir_file_exists(data_dir(), "voicesc.snd")))
    {
        xmas = false;

        fprintf(stderr, "warning: Christmas is missing.\n");
    }

    JE_loadPals();

    JE_loadMainShapeTables(xmas ? "tyrianc.shp" : "tyrian.shp");
    if (xmas && !xmas_prompt())
    {
        xmas = false;

        free_main_shape_tables();
        JE_loadMainShapeTables("tyrian.shp");
    }

    /* Default Options */
    youAreCheating = false;
    smoothScroll = true;
    loadDestruct = false;

    if (!audio_disabled)
    {
        fprintf(stderr, "initializing SDL audio...\n");

        init_audio();

        load_music();
        JE_loadSndFile("tyrian.snd", xmas ? "voicesc.snd" : "voices.snd");
    }
    else
    {
        fprintf(stderr, "audio disabled\n");
    }

    if (record_demo) {
        fprintf(stderr, "demo recording enabled (input limited to keyboard)\n");
    }


    JE_loadExtraShapes();  /*Editship*/

    JE_loadHelpText();

    for (; ; )
    {
        JE_initPlayerData();
        JE_sortHighScores();

        if (JE_titleScreen(true))
            break;  // user quit from title screen

        if (loadDestruct)
        {
            JE_destructGame();
            loadDestruct = false;
        }
        else
        {
            JE_main();
        }
    }

    fprintf(stderr, "preparing to exit\n");
    JE_tyrianHalt(0);

    fprintf(stderr, "exiting\n");
    return 0;
}
