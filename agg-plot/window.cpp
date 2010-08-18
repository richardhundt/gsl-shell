
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "window-cpp.h"
#include "lua-draw.h"
#include "lua-cpp-utils.h"
#include "gs-types.h"
#include "object-refs.h"
#include "object-index.h"
#include "colors.h"
#include "lua-plot-cpp.h"
#include "split-parser.h"

__BEGIN_DECLS

static int window_new        (lua_State *L);
static int window_free       (lua_State *L);
static int window_split      (lua_State *L);
static int window_attach     (lua_State *L);

static const struct luaL_Reg window_functions[] = {
  {"window",        window_new},
  {NULL, NULL}
};

static const struct luaL_Reg window_methods[] = {
  {"attach",         window_attach        },
  {"split",          window_split         },
  {"update",         window_update        },
  {"__gc",           window_free       },
  {NULL, NULL}
};

__END_DECLS

void window::ref::compose(bmatrix& a, const bmatrix& b)
{
  trans_affine_compose (a, b);
};

int window::ref::calculate(window::ref::node* t, const bmatrix& m, int id)
{
  ref *r = t->content();
  if (r)
    {
      r->slot_id = id++;
      r->matrix = m;
    }

  int nb = list::length(t->tree());

  if (nb > 0)
    {
      double frac = 1 / (double) nb;

      direction_e dir;
      ref::node::list *ls = t->tree(dir);
      if (ls)
	{
	  bmatrix lm;

	  double* p = (dir == along_x ? &lm.tx : &lm.ty);
	  double* s = (dir == along_x ? &lm.sx : &lm.sy);

	  *s = frac;

	  for ( ; ls; ls = ls->next(), *p += frac)
	    {
	      bmatrix sm(lm);
	      window::ref::compose(sm, m);
	      id = window::ref::calculate (ls->content(), sm, id);
	    }
	}
    }

  return id;
}

static void remove_plot_ref (lua_State *L, int window_index, int plot_id);

void
window::clear_box(const bmatrix& m)
{
  int x1 = (int) m.tx, y1 = (int) m.ty;
  int x2 = (int) (m.tx + m.sx), y2 = (int) (m.ty + m.sy);
  m_canvas->clear_box(x1, y1, x2 - x1, y2 - y1);
}

void
window::draw_rec(ref::node *n)
{
  ref::node::list *ls;
  for (ls = n->tree(); ls != NULL; ls = ls->next())
    draw_rec(ls->content());

  ref *ref = n->content();
  if (ref)
    {
      if (ref->plot)
	{
	  agg::trans_affine mtx(ref->matrix);
	  this->scale(mtx);
	  ref->plot->draw(*m_canvas, mtx);
	}
    }
}

window::ref* window::ref_lookup (ref::node *p, int slot_id)
{
  ref::node::list *t = p->tree();
  for (/* */; t; t = t->next())
    {
      ref *ref = window::ref_lookup(t->content(), slot_id);
      if (ref)
	return ref;
    }

  ref *ref = p->content();
  if (ref)
    {
      if (ref->slot_id == slot_id)
	return ref;
    }

  return NULL;
}


void
window::draw_slot(int slot_id)
{
  ref *ref = window::ref_lookup (this->m_tree, slot_id);
  if (ref && m_canvas)
    {
      agg::trans_affine mtx(ref->matrix);
      this->scale(mtx);
      this->clear_box(mtx);
      ref->plot->draw(*m_canvas, mtx);
    }
}

void
window::on_draw_unprotected()
{
  if (! m_canvas)
    return;

  m_canvas->clear();
  draw_rec(m_tree);
}

void
window::on_draw()
{
  AGG_LOCK();
  on_draw_unprotected();
  AGG_UNLOCK();
}

window *
window::check (lua_State *L, int index)
{
  return (window *) gs_check_userdata (L, index, GS_WINDOW);
}

void
window::cleanup_tree_rec (lua_State *L, int window_index, ref::node* n)
{
  for (ref::node::list *ls = n->tree(); ls != NULL; ls = ls->next())
    cleanup_tree_rec(L, window_index, ls->content());

  ref *ref = n->content();
  if (ref)
    {
      if (ref->plot)
	remove_plot_ref (L, window_index, ref->plot_id);
    }
}

void
window::split(const char *spec)
{
  if (m_tree)
    delete m_tree;

  ::split<ref>::lexer lexbuf(spec);
  m_tree = ::split<ref>::parse(lexbuf);

  bmatrix m0;
  ref::calculate(m_tree, m0, 0);
}

static const char *
next_int (const char *str, int& val)
{
  while (*str == ' ')
    str++;
  if (*str == '\0')
    return NULL;

  char *eptr;
  val = strtol (str, &eptr, 10);

  if (eptr == str)
    return NULL;

  while (*eptr == ' ')
    eptr++;
  if (*eptr == ',')
    eptr++;
  return eptr;
}

/* Returns the existing plot ref id, 0 if there isn't any.
   It does return -1 in case of error.*/
int window::attach(lua_plot *plot, const char *spec, int plot_id, int& slot_id)
{
  ref::node *n = m_tree;
  const char *ptr;
  int k;

  for (ptr = next_int (spec, k); ptr; ptr = next_int (ptr, k))
    {
      ref::node::list* list = n->tree();

      if (! list)
	return -1;

      for (int j = 1; j < k; j++)
	{
	  list = list->next();
	  if (! list)
	    return -1;
	}

      n = list->content();
    }

  ref* r = n->content();
  if (! r)
    return -1;

  int ex_id = r->plot_id;

  r->plot = & plot->self();
  r->plot_id = plot_id;

  slot_id = r->slot_id;

  return (ex_id > 0 ? ex_id : 0);
}

int
window_new (lua_State *L)
{
  window *win = new(L, GS_WINDOW) window(colors::white);
  win->start_new_thread (L);
  return 1;
}

int
window_free (lua_State *L)
{
  window *win = window::check (L, 1);
  win->~window();
  return 0;
}


int
window_split (lua_State *L)
{
  window *win = window::check (L, 1);
  const char *spec = luaL_checkstring (L, 2);

  win->lock();

  win->cleanup_refs(L, 1);
  win->split(spec);

  win->on_draw();
  win->update_window();

  win->unlock();
  return 0;
}

void 
remove_plot_ref (lua_State *L, int window_index, int plot_id)
{
  object_index_get (L, OBJECT_PLOT, plot_id);

  int plot_index = lua_gettop (L);
  if (gs_is_userdata (L, plot_index, GS_PLOT))
    object_ref_remove (L, window_index, plot_index);
}

int
window_attach (lua_State *L)
{
  window *win = window::check (L, 1);
  lua_plot *plot = lua_plot::check (L, 2);
  const char *spec = luaL_checkstring (L, 3);
  int slot_id;

  win->lock();

  int ex_plot_id = win->attach (plot, spec, plot->id, slot_id);

  if (ex_plot_id >= 0)
    {
      plot->window_id = win->id;
      plot->slot_id = slot_id;

      win->on_draw();
      win->update_window();

      win->unlock();

      object_ref_add (L, 1, 2);

      if (ex_plot_id > 0)
	remove_plot_ref (L, 1, ex_plot_id);
    }
  else
    {
      win->unlock();
      luaL_error (L, "invalid slot");
    }

  return 0;
}

int
window_slot_update_unprotected (lua_State *L)
{
  window *win = window::check (L, 1);
  int slot_id = luaL_checkinteger (L, 2);

  win->lock();
  win->draw_slot(slot_id);
  win->update_window();
  win->unlock();

  return 0;
}

int
window_update_unprotected (lua_State *L)
{
  window *win = window::check (L, 1);

  win->lock();
  win->on_draw_unprotected();
  win->update_window();
  win->unlock();

  return 0;
}

int
window_update (lua_State *L)
{
  window *win = window::check (L, 1);

  win->lock();
  win->on_draw();
  win->update_window();
  win->unlock();

  return 0;
}

void
window_register (lua_State *L)
{
  luaL_newmetatable (L, GS_METATABLE(GS_WINDOW));
  lua_pushvalue (L, -1);
  lua_setfield (L, -2, "__index");
  luaL_register (L, NULL, window_methods);
  lua_pop (L, 1);

  /* gsl module registration */
  luaL_register (L, NULL, window_functions);
}
