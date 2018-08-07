# Basic networking with RIOT and Lua

## What?

This application contains 3 lua modules:

- repl: provides the interactive shell.
- riot: Access RIOT system functionality. Currently, the only function provided
     is `shell()` for running shell commands.
- socket: Provides bindings for sock. Only udp and ipv6 are currently supported.

## Example session

```lua
main(): This is RIOT! (Version: 2018.07-devel-1311-g8f5e6-batman-lua-library-temp)
Using memory range for Lua heap: 0x5667f2b0 - 0x56688ef0, 4 bytes
This is Lua: starting interactive session

Welcome to the interactive interpreter

L> r = require"riot"
L> r.shell("ifconfig")
Iface  5  HWaddr: e2:fa:5d:a5:e8:f8
          MTU:1500  HL:64  Source address length: 6
          Link type: wired
          inet6 addr: fe80::e0fa:5dff:fea5:e8f8  scope: local  VAL
          inet6 group: ff02::1
          inet6 group: ff02::1:ffa5:e8f8

0
L> s = require"socket"
L> u=s.udp()
L> u
sock_udp: 0x566b5604
L> u:send("hello", {address="fe80::4c83:2cff:fe68:69c", port=7894, netif=5})
5
L> u:close()
L> u = s.udp({address="::", port=1235})
L> m, b = u:revc(10, -1)
L> m
fsfewwed
```

