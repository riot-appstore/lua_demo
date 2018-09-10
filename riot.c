/*
 * Copyright (C) 2018 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     lua
 * @{
 *
 * @file
 * @brief       Access low level RIOT subsystems from within lua.
 *
 * @author      Juan Carrano <j.carrano@fu-berlin.de>
 *
 * @}
 */

#define LUA_LIB

#include "lprefix.h"

#include "shell.h"
#include "xtimer.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/**
 * Run a shell command.
 *
 * Takes multiple string arguments.
 * The first string is the name of the command and the rest are the command line
 * arguments.
 *
 * @return    Exit status, or nil if the command was not found.
 */
int _shell(lua_State *L)
{
    int argc = lua_absindex (L, -1);
    int i, retval;
    const char *argv[argc];

    if (argc < 1) {
        return luaL_error(L, "Expected at least one argument");
    }

    for (i = 0; i < argc; i++) {
        argv[i] = luaL_checklstring (L, i+1, NULL);
    }

    retval = shell_call(argc, (char **)argv);

    if (retval == -1) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, retval);
    }

    return 1;
}

/**
 * Sleep for a (maybe fractional) number of seconds.
 */
int _sleep(lua_State *L)
{
    lua_Number s = luaL_checknumber(L, 1);

    if (s > 0) {
        xtimer_usleep(s * (1000*1000));
    }

    return 0;
}

static const luaL_Reg funcs[] = {
  {"shell", _shell},
  {"sleep", _sleep},
  /* placeholders */
  {"BOARD", NULL},
  {"MCU", NULL},
  {"VERSION", NULL},
  {NULL, NULL}
};

/**
 * Load the library.
 *
 * @return      Lua table.
 */
int luaopen_riot(lua_State *L)
{
    luaL_newlib(L, funcs);

    lua_pushliteral(L, RIOT_BOARD);
    lua_setfield(L, -2, "BOARD");

    lua_pushliteral(L, RIOT_MCU);
    lua_setfield(L, -2, "MCU");

    lua_pushliteral(L, RIOT_VERSION);
    lua_setfield(L, -2, "VERSION");

    return 1;
}
