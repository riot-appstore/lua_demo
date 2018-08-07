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
 * @brief       Basic UDP sockets.
 *
 * Lua bindings for UDP sock.
 *
 * @author      Juan Carrano <j.carrano@fu-berlin.de>
 *
 * @}
 */

#define LUA_LIB

#include "lprefix.h"

#include "net/sock/udp.h"
#include "net/sock/util.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* MetaTable names */
#define SOCK_UDP_TNAME "sock_udp"

enum EP_PARSE_RESULT { EP_NULL, EP_PARSED, EP_ERROR};

/**
 * Push a nil and a string to the lua stack.
 */
static void _nil_and_str(lua_State *L, const char *s)
{
    lua_pushnil(L);
    lua_pushstring(L, s);
}

/**
 * Get a 16 bit number from a table.
 *
 * If the table entry is nil, it does nothing and returns EP_NULL.
 *
 * @return  EP_NULL, EP_PARSED  on success. EP_ERROR on error.
 */
static enum EP_PARSE_RESULT _field2int(lua_State *L, int index, const char *key,
                                       uint16_t *result)
{
    if (lua_getfield (L, index, key) != LUA_TNIL) {
        int isnum;
        lua_Number pn = lua_tonumberx(L, -1, &isnum);

        if (!isnum) {
            _nil_and_str(L, "Cannot convert object to number");
            return EP_ERROR;
        } else if (pn >=0 && pn <= UINT16_MAX){
            *result = pn;
            lua_pop(L, 1);
            return EP_PARSED;
        } else {
            _nil_and_str(L, "Number off-range (must be 16 bit)");
            return EP_ERROR;
        }
    }

    return EP_NULL;
}

/**
 * Convert a string or a table into a sock_udp_ep_t.
 *
 * @param   index   Index of the string or table in the stack.
 *
 * @return  EP_NULL   If the endpoint should be null
 * @return  EP_PARSED   If the endpoint was parsed and the result is in *ep
 * @return  EP_ERROR    If there was an error parsing the endpoint. A nil and
 *                      a message will be pushed to the stack.
 */
static enum EP_PARSE_RESULT _parse_udp_endpoint(lua_State *L, int index, sock_udp_ep_t *ep)
{
    switch (lua_type(L, index)) {
        case LUA_TNONE: /* falls through */
        case LUA_TNIL:
            return EP_NULL;
        case LUA_TSTRING:
            {
                const char *s = lua_tolstring(L, index, NULL);
                if (sock_udp_str2ep(ep, s) == 0) {
                    return EP_PARSED;
                } else {
                    _nil_and_str(L, "Address/port badly formatted");
                    return EP_ERROR;
                }
            }
            break;
        default: /* table-like */
            ep->port = 0;
            ep->netif = SOCK_ADDR_ANY_NETIF;
            ep->family = AF_UNSPEC;

            if (_field2int(L, index, "port", &ep->port) == EP_ERROR) {
                return EP_ERROR;
            }

            if (_field2int(L, index, "netif", &ep->netif) == EP_ERROR) {
                return EP_ERROR;
            }

            if (lua_getfield (L, index, "address")  != LUA_TNIL) {
                size_t slen;
                const char *addr = lua_tolstring (L, -1, &slen);

                ep->family = AF_INET6;

                if (!slen
                    || ipv6_addr_from_str((ipv6_addr_t*)&ep->addr.ipv6, addr) == NULL) {
                    _nil_and_str(L, "Address badly formatted");
                    return EP_ERROR;
                }

                lua_pop(L, 1);
            }
            return EP_PARSED;
    }
}

/**
 * Create a new UDP socket.
 *
 * @param   local   Local endpoint (as table or string, can be nil).
 * @param   remote  Remote endpoint (as table or string, can be nil).
 * @param   flags   Additional parameters after the remote endpoint will be
 *                  interpreted as flags. Not implemented yet.
 * @return  UDP socket object (full userdata with custom metatable)
 */
static int udp_new(lua_State *L)
{
    uint16_t flags = 0;
    int retval;
    sock_udp_ep_t local, remote;
    sock_udp_ep_t *plocal, *premote;

    switch (_parse_udp_endpoint(L, 1, &local)) {
        default:
        case EP_NULL:
            plocal = NULL;
            break;
        case EP_PARSED:
            plocal = &local;
            break;
        case EP_ERROR:
            return 2; /* 2 return values, a nil and a message */
    }

    switch (_parse_udp_endpoint(L, 2, &remote)) {
        default:
        case EP_NULL:
            premote = NULL;
            break;
        case EP_PARSED:
            premote = &remote;
            break;
        case EP_ERROR:
            return 2; /* 2 return values, a nil and a message */
    }

    sock_udp_t *s = lua_newuserdata(L, sizeof(*s));

    retval = sock_udp_create(s, plocal, premote, flags);

    if (retval != 0) {
        lua_pushnil(L);
    }
    switch (retval) {
        case -EINVAL:
            lua_pushliteral(L, "Invalid endpoints");
            break;
        case -EAFNOSUPPORT:
            lua_pushliteral(L, "Socket type not supported");
            break;
        case -EADDRINUSE:
            lua_pushliteral(L, "Address in use");
            break;
        default:
            lua_pushliteral(L, "Unknown error");
            break;
        case 0:
            break;
    }

    if (retval != 0) {
        return 2;
    }

    luaL_setmetatable(L, SOCK_UDP_TNAME);

    return 1;
}

/**
 * Receive data from a UDP socket.
 *
 * @param   sock
 * @param   n              Receive up to n bytes.
 * @param   timeout_ms     Use 0 to return immediately, -1 for no timeout.
 * @param   remote (optional) remote end point.
 *
 * @return  Received data as string, or nil+error message.
 */
static int udp_recv(lua_State *L)
{
    /* s cannot be NULL */
    sock_udp_t *s = luaL_checkudata(L, 1, SOCK_UDP_TNAME);
    int n = luaL_checkinteger(L, 2);
    int timeout = luaL_checkinteger(L, 3);
    sock_udp_ep_t remote, *premote;

    switch (_parse_udp_endpoint(L, 4, &remote)) {
        default:
        case EP_NULL:
            premote = NULL;
            break;
        case EP_PARSED:
            premote = &remote;
            break;
        case EP_ERROR:
            return 2; /* 2 return values, a nil and a message */
    }

    /* This is wasteful: we are allocating an array and then copying the contents
     * to a smaller array instead of resizing, but AFAIK it is not possible to
     * preallocate and then shrink a string in lua.
     */
    void *buf = lua_newuserdata(L, n);
    ssize_t nrecv = sock_udp_recv(s, buf, n, timeout, premote);

    if (nrecv < 0) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushliteral(L, "error, i'm to lazy to report properly");
        return 2;
    } else {
        lua_pushlstring(L, buf, nrecv);
        lua_replace(L, -2); /*this is to get rid of the userdata */
        return 1;
    }
}

/**
 * Send data through a udp socket.
 *
 * @param   sock
 * @param   data    String containing the data to be sent.
 * @param   remote  (optional) remote end point.
 *
 * @return  number of bytes sent.
 */
static int udp_send(lua_State *L)
{
    sock_udp_t *s = luaL_checkudata(L, 1, SOCK_UDP_TNAME);
    size_t len;
    ssize_t sent;
    const char *data = luaL_checklstring(L, 2, &len);
    sock_udp_ep_t remote, *premote;

    switch (_parse_udp_endpoint(L, 3, &remote)) {
        default:
        case EP_NULL:
            premote = NULL;
            break;
        case EP_PARSED:
            premote = &remote;
            break;
        case EP_ERROR:
            return 2; /* 2 return values, a nil and a message */
    }

    sent = sock_udp_send(s, data, len, premote);

    if (sent < 0) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushliteral(L, "error, i'm to lazy to report properly");
        return 2;
    } else {
        lua_pushinteger(L, sent);
        return 1;
    }
}

/**
 * Close a UDP socket.
 */
static int udp_close(lua_State *L)
{
    /* s cannot be NULL */
    sock_udp_t *s = luaL_checkudata(L, 1, SOCK_UDP_TNAME);

    sock_udp_close(s);

    return 0;
}

static const luaL_Reg udp_methods[] = {
  {"close", udp_close},
  {"recv", udp_recv},
  {"send", udp_send},
  {NULL, NULL}
};

static const luaL_Reg funcs[] = {
  {"udp", udp_new},
  /* placeholders */
  {"REUSE_EP", NULL},
  {NULL, NULL}
};

/**
 * Load the library.
 *
 * @return      Lua table.
 */
int luaopen_socket(lua_State *L)
{
    if (luaL_newmetatable(L, SOCK_UDP_TNAME)) {
        luaL_newlib(L, udp_methods);
        lua_setfield(L, -2, "__index");
    }

    luaL_newlib(L, funcs);

    lua_pushinteger(L, SOCK_FLAGS_REUSE_EP);
    lua_setfield(L, -2, "REUSE_EP");

    return 1;
}
