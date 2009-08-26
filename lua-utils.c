
/* lua-utils.c
 * 
 * Copyright (C) 2009 Francesco Abbate
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>
#include "lua-utils.h"

char const * const CACHE_FIELD_NAME = "__cache";

const struct luaL_Reg *
mlua_find_method (const struct luaL_Reg *p, const char *key)
{
  for (/* */; p->name; p++)
    {
      if (strcmp (p->name, key) == 0)
	return p;
    }
  return NULL;
}

int
mlua_get_property (lua_State *L, const struct luaL_Reg *p, bool use_cache)
{
  int rval;
  bool cache_is_new = false;

  if (! use_cache)
    return p->func (L);

  lua_getfield (L, 1, CACHE_FIELD_NAME);
  if (lua_isnil (L, -1))
    {
      lua_pop (L, 1);
      lua_newtable (L);
      lua_pushvalue (L, -1);
      lua_setfield (L, 1, CACHE_FIELD_NAME);
      cache_is_new = true;
    }

  if (! cache_is_new)
    {
      lua_getfield (L, -1, p->name);
      if (! lua_isnil (L, -1))
	return 1;
      lua_pop (L, 1);
    }

  rval = p->func (L);
  if (rval == 1)
    {
      lua_pushvalue (L, -1);
      lua_setfield (L, -3, p->name);
      return 1;
    }
  return rval;
}

void
mlua_null_cache (lua_State *L, int index)
{
  lua_pushnil (L);
  lua_setfield (L, index, CACHE_FIELD_NAME);
}
