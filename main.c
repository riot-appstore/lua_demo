/*
 * Copyright (C) 2018 Freie Universität Berlin.
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Lua shell in RIOT
 *
 * @author      Juan Carrano <j.carrano@fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>

#include "periph/pwm.h"
#include "servo.h"
#include "tsl4531x.h"
#include "tsl4531x_saul.h"
#include "saul.h"
#include "saul_reg.h"

#include "lua_run.h"
#include "lua_builtin.h"
#include "repl.lua.h"

/* The basic interpreter+repl needs about 13k ram AT Minimum but we need more
 * memory in order to do interesting stuff.
 */
#define MAIN_LUA_MEM_SIZE (40000)

static char lua_memory[MAIN_LUA_MEM_SIZE] __attribute__ ((aligned(__BIGGEST_ALIGNMENT__)));

#define BARE_MINIMUM_MODS (LUAR_LOAD_BASE | LUAR_LOAD_IO | LUAR_LOAD_PACKAGE | LUAR_LOAD_MATH)

const struct lua_riot_builtin_lua _lua_riot_builtin_lua_table[] = {
    { "repl", repl_lua, sizeof(repl_lua) }
};

extern int luaopen_socket(lua_State *L);
extern int luaopen_riot(lua_State *L);
extern int luaopen_saul(lua_State *L);

const struct lua_riot_builtin_c _lua_riot_builtin_c_table[] = {
    { "riot", luaopen_riot},
    { "saul", luaopen_saul},
    { "socket", luaopen_socket}
};

const struct lua_riot_builtin_lua *const lua_riot_builtin_lua_table = _lua_riot_builtin_lua_table;
const struct lua_riot_builtin_c *const lua_riot_builtin_c_table = _lua_riot_builtin_c_table;

const size_t lua_riot_builtin_lua_table_len = 1;
const size_t lua_riot_builtin_c_table_len = 3;


static int write_servo(const void *dev, phydat_t *res)
{
    servo_set(dev, res->val[0]);
    return 1;
}

const saul_driver_t servo_saul_driver = {
    .read = saul_notsup,
    .write = write_servo,
    .type = SAUL_ACT_SERVO
};

int main(void)
{
    int res;
    servo_t servo;
    tsl4531x_t lux_sensor;
    saul_reg_t servo_reg = {.dev = &servo, .name = "Servomotor",
                            .driver=&servo_saul_driver},
               lux_reg = {.dev = &lux_sensor, .name = "TSL45315",
                          .driver = &tsl4531x_saul_driver};

    res = servo_init(&servo, PWM_DEV(0), 0, 1000, 2000);
    if (res < 0) {
        puts("Errors while initializing servo");
        return -1;
    }
    puts("Servo initialized.");

    if (saul_reg_add(&servo_reg) < 0) {
        puts("Failed to register servo");
        return -1;
    }
    puts("Servo registered.");

    res = tsl4531x_init(&lux_sensor, TSL4531_I2C_PORT, TSL4531x_INTEGRATE_100ms);
    if (res < 0) {
        puts("Errors while initializing light sensor");
        return -1;
    }
    puts("Light sensor initialized.");

    if (saul_reg_add(&lux_reg) < 0) {
        puts("Failed to register light sensor");
        return -1;
    }
    puts("Light sensor registered.");

    printf("Using memory range for Lua heap: %p - %p, %zu bytes\n",
           lua_memory, lua_memory + MAIN_LUA_MEM_SIZE, sizeof(void *));

    while (1) {
        int status, value;
        puts("This is Lua: starting interactive session\n");

        status = lua_riot_do_module("repl", lua_memory, MAIN_LUA_MEM_SIZE,
                                    BARE_MINIMUM_MODS, &value);

        printf("Exited. status: %s, return code %d\n", lua_riot_strerror(status),
               value);
    }

    return 0;
}
