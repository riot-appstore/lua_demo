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
 * @brief       Saul Registry
 *
 * @author      Juan Carrano <j.carrano@fu-berlin.de>
 *
 * @}
 */

#define LUA_LIB

#include "lprefix.h"

#include "saul_reg.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "binsearch.h"

#include <math.h>
#include <stdio.h>

#define CACHE_TABLE "_devcache"
#define SAULDEV_TNAME "saul_dev"

#define MAX_ENUM_LEN 64
#define N_ELEM(a) (sizeof(a)/sizeof(*(a)))

struct named_byte {
    const char *name;
    uint8_t value;
};

/* This should be sorted */
static struct named_byte devtype2code[] = {
    { "ACT_ANY", SAUL_ACT_ANY },
    { "ACT_DIMMER", SAUL_ACT_DIMMER },
    { "ACT_LED_RGB", SAUL_ACT_LED_RGB },
    { "ACT_MOTOR", SAUL_ACT_MOTOR },
    { "ACT_SERVO", SAUL_ACT_SERVO },
    { "ACT_SWITCH", SAUL_ACT_SWITCH },
    { "CLASS_ANY", SAUL_CLASS_ANY },
    { "CLASS_UNDEF", SAUL_CLASS_UNDEF },
    { "SENSE_ACCEL", SAUL_SENSE_ACCEL },
    { "SENSE_ANALOG", SAUL_SENSE_ANALOG },
    { "SENSE_ANY", SAUL_SENSE_ANY },
    { "SENSE_BTN", SAUL_SENSE_BTN },
    { "SENSE_CO2", SAUL_SENSE_CO2 },
    { "SENSE_COLOR", SAUL_SENSE_COLOR },
    { "SENSE_COUNT", SAUL_SENSE_COUNT },
    { "SENSE_DISTANCE", SAUL_SENSE_DISTANCE },
    { "SENSE_GYRO", SAUL_SENSE_GYRO },
    { "SENSE_HUM", SAUL_SENSE_HUM },
    { "SENSE_LIGHT", SAUL_SENSE_LIGHT },
    { "SENSE_MAG", SAUL_SENSE_MAG },
    { "SENSE_OBJTEMP", SAUL_SENSE_OBJTEMP },
    { "SENSE_OCCUP", SAUL_SENSE_OCCUP },
    { "SENSE_PRESS", SAUL_SENSE_PRESS },
    { "SENSE_TEMP", SAUL_SENSE_TEMP },
    { "SENSE_TVOC", SAUL_SENSE_TVOC },
    { "SENSE_UV", SAUL_SENSE_UV },
};

/**
 * Convert a saul device pointer into a lua object.
 *
 * This function is memoized using the _devcache weak table.
 * Leaves the metatable, followed by the required object on the stack.
 *
 * A null pointer results in a nil object.
 */
static void sauldev_to_lua(lua_State *L, saul_reg_t *dev)
{
    if (dev == NULL) {
        lua_pushnil(L);
        return;
    } else {
        lua_getfield(L, LUA_REGISTRYINDEX, CACHE_TABLE);
        lua_pushlightuserdata(L, dev);
    }

    if (lua_gettable(L, -2) == LUA_TNIL) {
        lua_pop(L, 1);

        saul_reg_t **reg = lua_newuserdata(L, sizeof(*dev));
        *reg = dev;

        luaL_setmetatable(L, SAULDEV_TNAME);
        lua_pushlightuserdata(L, dev);
        lua_pushvalue(L, -2); /* copy the device object over the key */

        /* At this point we have.
         * TABLE, DEVICE, LIGHT UD, DEVICE */
        lua_settable(L, -4);
    }
}

static int get_name(lua_State *L)
{
    saul_reg_t **d = luaL_checkudata(L, 1, SAULDEV_TNAME);

    lua_pushstring(L, (*d)->name);

    return 1;
}

static int get_type(lua_State *L)
{
    saul_reg_t **d = luaL_checkudata(L, 1, SAULDEV_TNAME);
    uint8_t type = (*d)->driver->type;
    unsigned int i;

    for (i = 0; i < N_ELEM(devtype2code); i++) {
        if (devtype2code[i].value == type) {
            lua_pushstring(L, devtype2code[i].name);
            return 1;
        }
    }

    lua_pushliteral(L, "CLASS_UNDEF");

    return 1;
}

static float exp10fi(int exponent)
{
    int n;
    float r = 1.0f;

    if (exponent >= 0) {
        for (n = 0; n < exponent; n++) {
            r *= 10.0f;
        }
    } else {
        for (n = 0; n < -exponent; n++) {
            r /= 10.0f;
        }
    }

    return r;
}

/**
 * Write values to a device.
 *
 * This takes the device as first argument and up to three additional values.
 * Values are floating point numbers.
 *
 * On error returns nil and a message.
 *
 * This has a small bug. The most negative value in a phydat is unusable.
 */
static int _write(lua_State *L)
{
    int i, n_params = lua_gettop(L) - 1;
    saul_reg_t **d = luaL_checkudata(L, 1, SAULDEV_TNAME);
    phydat_t data = { { 0, 0, 0 }, 0, 0 };
    float maxabs = 0, scale_factor = 1.0;

    for (i = 0; i < n_params; i++) {
        lua_Number n = fabsf(luaL_checknumber(L, i + 2));

        maxabs = fmaxf(maxabs, n);
    }

    /* Optimize dynamic range */
    /* super hacky hack: if there is only one parameter and it is an integer
     * that fits in the range, live it as is.*/
    if (n_params == 1 && maxabs == roundf(maxabs)) {
        scale_factor = 1;
    } else {
        if (maxabs > PHYDAT_MAX) {
            while (maxabs > PHYDAT_MAX) {
                maxabs /= 10;
                data.scale += 1;
            }
        } else {
            while (maxabs != 0 && maxabs * 10.0 < PHYDAT_MAX) {
                maxabs *= 10;
                data.scale -= 1;
            }
        }

        scale_factor = exp10fi(data.scale);
    }

    for (i = 0; i < n_params; i++) {
        lua_Number n = lua_tonumber(L, i + 2);

        data.val[i] = n/scale_factor;
    }

    int nprocessed = saul_reg_write(*d, &data);

    if (nprocessed >= 0) {
        lua_pushinteger(L, nprocessed);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushfstring(L, "error %d", nprocessed);
        return 2;
    }
}

/**
 * Read the device.
 *
 * This function returns up to three values (floating point numbers).
 */
static int _read(lua_State *L)
{
    saul_reg_t **d = luaL_checkudata(L, 1, SAULDEV_TNAME);
    phydat_t data;
    int n, nread;

    data.scale = 0;
    nread = saul_reg_read(*d, &data);

    if (nread < 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "error %d", nread);
        return 2;
    }

    float fscale = exp10fi(data.scale);

    for (n = 0; n < nread; n++) {
        lua_pushnumber(L, data.val[n] * fscale);
    }

    return nread;
}

static const luaL_Reg saul_dev_methods[] = {
    {"get_name", get_name},
    {"get_type", get_type},
    {"read", _read},
    {"write", _write},
    {NULL, NULL}
};

/**
 * __index metamethod for the module table.
 *
 * Try to find the key in the enum list, and if not found, search a device with
 * that name.
 */
static int _index(lua_State *L)
{
    const char * key = luaL_checkstring(L, 2);

    saul_reg_t *dev = saul_reg_find_name(key);
    sauldev_to_lua(L, dev);

    return 1;
}

/**
 * Find the first device of the given type.
 *
 * @param   type    String (see saulreg.types for valid values).
 *
 * @return  nil     if no device is found.
 * @return          first device matching type.
 */
static int find_type(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    const uint8_t *enum_value;

    if ((enum_value = BINSEARCH_STR_P(devtype2code, N_ELEM(devtype2code), name,
                                      s, MAX_ENUM_LEN)) != NULL) {
        saul_reg_t *dev = saul_reg_find_type(*enum_value);
        sauldev_to_lua(L, dev);

        return 1;
    } else {
        return luaL_error(L, "Unknown device type");
    }
}

/**
 * List all the device types.
 *
 * @return  Table with strings.
 */
static int all_types(lua_State *L)
{
    unsigned int i;

    lua_createtable(L, N_ELEM(devtype2code), 0);

    for (i = 0; i < N_ELEM(devtype2code); i++) {
        lua_pushstring(L, devtype2code[i].name);
        lua_rawseti(L, -2, i);
    }

    return 1;
}


/* For this module we are going to cheat and provide the contents via metatable
 * methods. There's not point in populating the table with all devices, and we
 * already have functions for searching provided by saul_reg.
 */

static const luaL_Reg funcs[] = {
  {"find_type", find_type},
  {"types", all_types},
  {"__index", _index},
  /* placeholders */
  {NULL, NULL}
};

/**
 * Load the library.
 *
 * @return      Lua table.
 */
int luaopen_saul(lua_State *L)
{
    if (luaL_newmetatable(L, SAULDEV_TNAME)) {
        luaL_setfuncs(L, saul_dev_methods, 0);
        lua_setfield(L, -1, "__index");
    }

    lua_newtable(L);

    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);

    lua_setfield(L, LUA_REGISTRYINDEX, CACHE_TABLE);

    luaL_newlib(L, funcs);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);

    return 1;
}
