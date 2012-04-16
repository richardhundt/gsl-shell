#ifndef LUA_GRAPH_H
#define LUA_GRAPH_H

#include "defs.h"

__BEGIN_DECLS

#include <lua.h>

extern void register_graph (lua_State *L);
extern void graph_close_windows (lua_State *L);

__END_DECLS

#endif
