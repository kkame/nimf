/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-wayland.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2017-2019 Hodong Kim <cogniti@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"
#include "nimf-wayland-ic.h"
#include "nimf-wayland.h"
#include "nimf-service.h"

static void     nimf_wayland_ibus_stop  (NimfWayland *wayland);
static gboolean nimf_wayland_ibus_start (NimfWayland *wayland);

G_DEFINE_DYNAMIC_TYPE (NimfWayland, nimf_wayland, NIMF_TYPE_SERVICE);

typedef struct
{
  GSource      source;
  NimfWayland *wayland;
  GPollFD      poll_fd;
  gboolean     reading;
} NimfWaylandEventSource;

static gboolean nimf_wayland_source_prepare (GSource *base,
                                             gint    *timeout)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWaylandEventSource *source = (NimfWaylandEventSource *) base;

  *timeout = -1;

  if (source->reading)
    return FALSE;

  if (wl_display_prepare_read (source->wayland->display) != 0)
    return TRUE;

  source->reading = TRUE;

  if (wl_display_flush (source->wayland->display) < 0)
    g_critical (G_STRLOC ": %s: wl_display_flush() failed: %s", G_STRFUNC,
                g_strerror (errno));

  return FALSE;
}

static void nimf_wayland_stop (NimfService *service)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND (service);

  if (!wayland->active)
    return;

  if (wayland->event_source)
  {
    g_source_destroy (wayland->event_source);
    g_source_unref   (wayland->event_source);
    wayland->event_source = NULL;
  }

  nimf_wayland_ibus_stop (wayland);

  wayland->active = FALSE;
}

static gboolean nimf_wayland_source_check (GSource *base)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWaylandEventSource *source = (NimfWaylandEventSource *) base;

  if (source->reading)
  {
    if (source->poll_fd.revents & G_IO_IN)
    {
      if (wl_display_read_events (source->wayland->display) < 0)
      {
        g_critical (G_STRLOC ": %s: wl_display_read_events() failed: %s",
                    G_STRFUNC, g_strerror (errno));
        nimf_wayland_stop (NIMF_SERVICE (source->wayland));
      }
    }
    else
    {
      wl_display_cancel_read (source->wayland->display);
    }

    source->reading = FALSE;
  }

  return source->poll_fd.revents;
}

static gboolean nimf_wayland_source_dispatch (GSource     *base,
                                              GSourceFunc  callback,
                                              gpointer     user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWaylandEventSource *source = (NimfWaylandEventSource *) base;

  if (wl_display_dispatch_pending (source->wayland->display) < 0)
  {
    g_critical (G_STRLOC ": %s: wl_display_dispatch_pending() failed: %s: %m",
                G_STRFUNC, g_strerror (errno));
    nimf_wayland_stop (NIMF_SERVICE (source->wayland));
  }

  source->poll_fd.revents = 0;

  return TRUE;
}

static void nimf_wayland_source_finalize (GSource *base)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWaylandEventSource *source = (NimfWaylandEventSource *) base;

  if (source->reading)
    wl_display_cancel_read (source->wayland->display);

  source->reading = FALSE;
}

static GSourceFuncs event_funcs = {
  nimf_wayland_source_prepare,
  nimf_wayland_source_check,
  nimf_wayland_source_dispatch,
  nimf_wayland_source_finalize
};

GSource *
nimf_wayland_source_new (NimfWayland *wayland)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GSource *source;
  NimfWaylandEventSource *wl_source;

  source = g_source_new (&event_funcs, sizeof (NimfWaylandEventSource));
  wl_source = (NimfWaylandEventSource *) source;

  wl_source->wayland = wayland;
  wl_source->poll_fd.fd = wl_display_get_fd (wl_source->wayland->display);
  wl_source->poll_fd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
  g_source_add_poll (source, &wl_source->poll_fd);

  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_can_recurse (source, TRUE);

  return source;
}

static void
handle_surrounding_text (void *data,
                         struct zwp_input_method_context_v1 *context,
                         const char *text,
                         uint32_t cursor,
                         uint32_t anchor)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static void
handle_reset (void *data,
              struct zwp_input_method_context_v1 *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static void
handle_content_type (void *data,
                     struct zwp_input_method_context_v1 *context,
                     uint32_t hint,
                     uint32_t purpose)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static void
handle_invoke_action (void *data,
                      struct zwp_input_method_context_v1 *context,
                      uint32_t button,
                      uint32_t index)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static void
handle_commit_state (void *data,
                     struct zwp_input_method_context_v1 *context,
                     uint32_t serial)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = data;

  wayland->serial = serial;
}

static void
handle_preferred_language (void *data,
                           struct zwp_input_method_context_v1 *context,
                           const char *language)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static const struct zwp_input_method_context_v1_listener input_method_context_listener = {
  handle_surrounding_text,
  handle_reset,
  handle_content_type,
  handle_invoke_action,
  handle_commit_state,
  handle_preferred_language
};

static void
input_method_keyboard_keymap (void               *data,
                              struct wl_keyboard *wl_keyboard,
                              uint32_t            format,
                              int32_t             fd,
                              uint32_t            size)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = data;

  char *map_str;

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
  {
    close (fd);
    return;
  }

  map_str = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (map_str == MAP_FAILED)
  {
    close (fd);
    return;
  }

  wayland->keymap = xkb_keymap_new_from_string (wayland->xkb_context,
                                                map_str,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap (map_str, size);
  close (fd);

  if (!wayland->keymap)
  {
    g_critical (G_STRLOC ": %s: xkb_keymap_new_from_string() failed",
                G_STRFUNC);
    return;
  }

  wayland->state = xkb_state_new (wayland->keymap);
  if (!wayland->state)
  {
    g_critical (G_STRLOC ": %s: xkb_state_new() failed", G_STRFUNC);
    xkb_keymap_unref(wayland->keymap);
    return;
  }

  wayland->shift_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Shift");
  wayland->lock_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Lock");
  wayland->control_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Control");
  wayland->mod1_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Mod1");
  wayland->mod2_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Mod2");
  wayland->mod3_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Mod3");
  wayland->mod4_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Mod4");
  wayland->mod5_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Mod5");
  wayland->super_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Super");
  wayland->hyper_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Hyper");
  wayland->meta_mask =
    1 << xkb_keymap_mod_get_index (wayland->keymap, "Meta");
}

static void
input_method_keyboard_key (void *data,
                           struct wl_keyboard *wl_keyboard,
                           uint32_t serial,
                           uint32_t time,
                           uint32_t key,
                           uint32_t state_w)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = data;
  uint32_t code;
  uint32_t num_syms;
  const xkb_keysym_t *syms;
  xkb_keysym_t sym;
  enum wl_keyboard_key_state state = state_w;
  guint32 modifiers;
  NimfEvent *event;

  if (!wayland->state)
    return;

  code = key + 8;
  num_syms = xkb_state_key_get_syms (wayland->state, code, &syms);

  sym = XKB_KEY_NoSymbol;
  if (num_syms == 1)
    sym = syms[0];

  event = nimf_event_new (NIMF_EVENT_NOTHING);

  modifiers = wayland->modifiers;
  if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
  {
      modifiers |= NIMF_RELEASE_MASK;
      event->key.type = NIMF_EVENT_KEY_RELEASE;
  }
  else
  {
    event->key.type = NIMF_EVENT_KEY_PRESS;
  }

  event->key.state = modifiers;
  event->key.keyval = sym;
  event->key.hardware_keycode = code;

  if (!nimf_service_ic_filter_event (NIMF_SERVICE_IC (wayland->wic), event))
    zwp_input_method_context_v1_key (wayland->context, serial, time, key, state);

  nimf_event_free (event);
}

static void
input_method_keyboard_modifiers (void *data,
                                 struct wl_keyboard *wl_keyboard,
                                 uint32_t serial,
                                 uint32_t mods_depressed,
                                 uint32_t mods_latched,
                                 uint32_t mods_locked,
                                 uint32_t group)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = data;
  struct zwp_input_method_context_v1 *context = wayland->context;
  xkb_mod_mask_t mask;

  xkb_state_update_mask (wayland->state, mods_depressed,
                         mods_latched, mods_locked, 0, 0, group);
  mask = xkb_state_serialize_mods (wayland->state,
                                   XKB_STATE_MODS_DEPRESSED |
                                   XKB_STATE_MODS_LATCHED);
  wayland->modifiers = 0;
  if (mask & wayland->shift_mask)
    wayland->modifiers |= NIMF_SHIFT_MASK;
  if (mask & wayland->lock_mask)
    wayland->modifiers |= NIMF_LOCK_MASK;
  if (mask & wayland->control_mask)
    wayland->modifiers |= NIMF_CONTROL_MASK;
  if (mask & wayland->mod1_mask)
    wayland->modifiers |= NIMF_MOD1_MASK;
  if (mask & wayland->mod2_mask)
    wayland->modifiers |= NIMF_MOD2_MASK;
  if (mask & wayland->mod3_mask)
    wayland->modifiers |= NIMF_MOD3_MASK;
  if (mask & wayland->mod4_mask)
    wayland->modifiers |= NIMF_MOD4_MASK;
  if (mask & wayland->mod5_mask)
    wayland->modifiers |= NIMF_MOD5_MASK;
  if (mask & wayland->super_mask)
    wayland->modifiers |= NIMF_SUPER_MASK;
  if (mask & wayland->hyper_mask)
    wayland->modifiers |= NIMF_HYPER_MASK;
  if (mask & wayland->meta_mask)
    wayland->modifiers |= NIMF_META_MASK;

  zwp_input_method_context_v1_modifiers (context, serial,
                                         mods_depressed, mods_depressed,
                                         mods_latched, group);
}

static const struct wl_keyboard_listener input_method_keyboard_listener = {
  input_method_keyboard_keymap,
  NULL, /* enter */
  NULL, /* leave */
  input_method_keyboard_key,
  input_method_keyboard_modifiers
};

static void
input_method_activate (void *data,
                       struct zwp_input_method_v1 *input_method,
                       struct zwp_input_method_context_v1 *context)
{
  g_debug (G_STRLOC ": %s: %p, %p", G_STRFUNC, input_method, context);

  NimfWayland *wayland = data;

  if (wayland->context)
    zwp_input_method_context_v1_destroy (wayland->context);

  wayland->serial = 0;
  wayland->context = context;
  zwp_input_method_context_v1_add_listener (context,
                                            &input_method_context_listener,
                                            wayland);
  wayland->keyboard = zwp_input_method_context_v1_grab_keyboard (context);
  wl_keyboard_add_listener (wayland->keyboard,
                            &input_method_keyboard_listener,
                            wayland);
}

static void
input_method_deactivate (void *data,
                         struct zwp_input_method_v1 *input_method,
                         struct zwp_input_method_context_v1 *context)
{
  g_debug (G_STRLOC ": %s: %p, %p", G_STRFUNC, input_method, context);

  NimfWayland *wayland = data;

  if (!wayland->context)
    return;

  nimf_service_ic_reset (NIMF_SERVICE_IC (wayland->wic));
  zwp_input_method_context_v1_destroy (wayland->context);
  wayland->context = NULL;
}

static const struct zwp_input_method_v1_listener input_method_listener = {
  input_method_activate,
  input_method_deactivate
};

static void
registry_handle_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            name,
                        const char         *interface,
                        uint32_t            version)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = data;

  if (!g_strcmp0 (interface, "zwp_input_method_v1"))
  {
    wayland->input_method =
      wl_registry_bind (registry, name, &zwp_input_method_v1_interface, 1);
    zwp_input_method_v1_add_listener (wayland->input_method,
                                      &input_method_listener, wayland);
  }
}

static void
registry_handle_global_remove (void *data, struct wl_registry *registry,
                               uint32_t name)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global,
  registry_handle_global_remove
};

static gchar *
nimf_wayland_find_ibus_address (void)
{
  const gchar *env_addr = g_getenv ("IBUS_ADDRESS");
  if (env_addr && env_addr[0] != '\0')
    return g_strdup (env_addr);

  gchar *bus_dir_path = g_build_filename (g_get_user_config_dir (), "ibus", "bus", NULL);
  GDir  *dir = g_dir_open (bus_dir_path, 0, NULL);
  if (!dir)
  {
    g_free (bus_dir_path);
    return NULL;
  }

  const gchar *name;
  gchar       *best_file = NULL;
  gint64       best_mtime = 0;

  while ((name = g_dir_read_name (dir)) != NULL)
  {
    if (!strstr (name, "wayland") && !strstr (name, "unix"))
      continue;
    gchar *full = g_build_filename (bus_dir_path, name, NULL);
    GFile *gf = g_file_new_for_path (full);
    GFileInfo *info = g_file_query_info (gf, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info)
    {
      gint64 mtime = (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
      if (strstr (name, "wayland"))
        mtime += 1000000000LL;
      if (mtime >= best_mtime)
      {
        best_mtime = mtime;
        g_free (best_file);
        best_file = g_strdup (full);
      }
      g_object_unref (info);
    }
    g_object_unref (gf);
    g_free (full);
  }
  g_dir_close (dir);
  g_free (bus_dir_path);

  if (!best_file)
    return NULL;

  gchar *contents = NULL;
  gchar *result = NULL;
  if (g_file_get_contents (best_file, &contents, NULL, NULL) && contents)
  {
    gchar **lines = g_strsplit (contents, "\n", -1);
    gint i;
    for (i = 0; lines[i] != NULL; i++)
    {
      if (g_str_has_prefix (lines[i], "IBUS_ADDRESS="))
      {
        result = g_strdup (lines[i] + strlen ("IBUS_ADDRESS="));
        g_strstrip (result);
        break;
      }
    }
    g_strfreev (lines);
    g_free (contents);
  }
  g_free (best_file);
  return result;
}

static void
nimf_wayland_on_engine_call (GDBusConnection       *conn,
                             const gchar           *sender,
                             const gchar           *obj_path,
                             const gchar           *iface_name,
                             const gchar           *method_name,
                             GVariant              *params,
                             GDBusMethodInvocation *inv,
                             gpointer               user_data)
{
  NimfWayland   *wayland = NIMF_WAYLAND (user_data);
  NimfServiceIC *ic      = NIMF_SERVICE_IC (wayland->wic);

  if (g_strcmp0 (method_name, "ProcessKeyEvent") == 0)
  {
    guint32 keyval = 0, keycode = 0, state = 0;
    g_variant_get (params, "(uuu)", &keyval, &keycode, &state);

    /* Ignore synthetic or already handled IBus events (bit 24 and 25) */
    if (state & ((1U << 24) | (1U << 25)))
    {
      g_dbus_method_invocation_return_value (inv, g_variant_new ("(b)", FALSE));
      return;
    }

    NimfEvent *ev = nimf_event_new (NIMF_EVENT_NOTHING);
    if (state & (1U << 30))
      ev->key.type = NIMF_EVENT_KEY_RELEASE;
    else
      ev->key.type = NIMF_EVENT_KEY_PRESS;

    ev->key.state            = state & NIMF_MODIFIER_MASK;
    ev->key.keyval           = keyval;
    ev->key.hardware_keycode = keycode + 8;

    gboolean handled = nimf_service_ic_filter_event (ic, ev);
    nimf_event_free (ev);

    g_dbus_method_invocation_return_value (inv, g_variant_new ("(b)", handled));
    return;
  }

  if (g_strcmp0 (method_name, "FocusIn") == 0 ||
      g_strcmp0 (method_name, "FocusInId") == 0 ||
      g_strcmp0 (method_name, "Enable") == 0)
  {
    nimf_service_ic_focus_in (ic);
    g_dbus_method_invocation_return_value (inv, NULL);
    return;
  }

  if (g_strcmp0 (method_name, "FocusOut") == 0 ||
      g_strcmp0 (method_name, "FocusOutId") == 0 ||
      g_strcmp0 (method_name, "Disable") == 0)
  {
    nimf_service_ic_focus_out (ic);
    g_dbus_method_invocation_return_value (inv, NULL);
    return;
  }

  if (g_strcmp0 (method_name, "Reset") == 0)
  {
    nimf_service_ic_reset (ic);
    g_dbus_method_invocation_return_value (inv, NULL);
    return;
  }

  if (g_strcmp0 (method_name, "SetCursorLocation") == 0)
  {
    NimfRectangle area = {0};
    g_variant_get (params, "(iiii)", &area.x, &area.y, &area.width, &area.height);
    nimf_service_ic_set_cursor_location (ic, &area);
    g_dbus_method_invocation_return_value (inv, NULL);
    return;
  }

  if (g_strcmp0 (method_name, "Destroy") == 0)
  {
    nimf_service_ic_reset (ic);
    g_dbus_method_invocation_return_value (inv, NULL);
    return;
  }

  g_dbus_method_invocation_return_value (inv, NULL);
}

static void
nimf_wayland_on_factory_call (GDBusConnection       *conn,
                              const gchar           *sender,
                              const gchar           *obj_path,
                              const gchar           *iface_name,
                              const gchar           *method_name,
                              GVariant              *params,
                              GDBusMethodInvocation *inv,
                              gpointer               user_data)
{
  NimfWayland *wayland = NIMF_WAYLAND (user_data);

  if (g_strcmp0 (method_name, "CreateEngine") == 0)
  {
    g_dbus_method_invocation_return_value (inv,
                                           g_variant_new ("(o)", wayland->ibus_engine_path));
    return;
  }

  g_dbus_method_invocation_return_value (inv, NULL);
}

static void
nimf_wayland_request_global_engine (NimfWayland *wayland)
{
  if (!wayland->ibus_conn)
    return;

  g_dbus_connection_call (wayland->ibus_conn,
                          "org.freedesktop.IBus",
                          "/org/freedesktop/IBus",
                          "org.freedesktop.IBus",
                          "SetGlobalEngine",
                          g_variant_new ("(s)", "nimf"),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          NULL,
                          NULL);
}

static void
nimf_wayland_on_global_engine_changed (GDBusConnection *conn,
                                       const gchar     *sender_name,
                                       const gchar     *object_path,
                                       const gchar     *interface_name,
                                       const gchar     *signal_name,
                                       GVariant        *parameters,
                                       gpointer         user_data)
{
  NimfWayland *wayland = NIMF_WAYLAND (user_data);
  const gchar *engine_name = NULL;

  if (parameters && g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
  {
    g_variant_get (parameters, "(&s)", &engine_name);
    if (g_strcmp0 (engine_name, "nimf") != 0)
      nimf_wayland_request_global_engine (wayland);
  }
}

static void
nimf_wayland_ibus_stop (NimfWayland *wayland)
{
  if (wayland->ibus_watch_timer_id)
  {
    g_source_remove (wayland->ibus_watch_timer_id);
    wayland->ibus_watch_timer_id = 0;
  }

  if (wayland->ibus_conn)
  {
    if (wayland->ibus_engine_Changed_sub_id)
    {
      g_dbus_connection_signal_unsubscribe (wayland->ibus_conn,
                                            wayland->ibus_engine_Changed_sub_id);
      wayland->ibus_engine_Changed_sub_id = 0;
    }
    if (wayland->ibus_factory_reg_id)
    {
      g_dbus_connection_unregister_object (wayland->ibus_conn,
                                           wayland->ibus_factory_reg_id);
      wayland->ibus_factory_reg_id = 0;
    }
    if (wayland->ibus_engine_reg_id)
    {
      g_dbus_connection_unregister_object (wayland->ibus_conn,
                                           wayland->ibus_engine_reg_id);
      wayland->ibus_engine_reg_id = 0;
    }
    g_object_unref (wayland->ibus_conn);
    wayland->ibus_conn = NULL;
  }

  g_clear_pointer (&wayland->ibus_engine_path, g_free);
}

static GDBusNodeInfo *
nimf_wayland_build_factory_node (void)
{
  const gchar *xml =
    "<node>"
    "  <interface name='org.freedesktop.IBus.Factory'>"
    "    <method name='CreateEngine'>"
    "      <arg name='name' direction='in' type='s'/>"
    "      <arg name='path' direction='out' type='o'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.IBus.Service'>"
    "    <method name='Destroy'/>"
    "  </interface>"
    "</node>";
  return g_dbus_node_info_new_for_xml (xml, NULL);
}

static GDBusNodeInfo *
nimf_wayland_build_engine_node (void)
{
  const gchar *xml =
    "<node>"
    "  <interface name='org.freedesktop.IBus.Engine'>"
    "    <method name='ProcessKeyEvent'>"
    "      <arg name='keyval' direction='in' type='u'/>"
    "      <arg name='keycode' direction='in' type='u'/>"
    "      <arg name='state' direction='in' type='u'/>"
    "      <arg name='handled' direction='out' type='b'/>"
    "    </method>"
    "    <method name='SetCursorLocation'>"
    "      <arg name='x' direction='in' type='i'/>"
    "      <arg name='y' direction='in' type='i'/>"
    "      <arg name='w' direction='in' type='i'/>"
    "      <arg name='h' direction='in' type='i'/>"
    "    </method>"
    "    <method name='SetCapabilities'><arg name='caps' direction='in' type='u'/></method>"
    "    <method name='FocusIn'/>"
    "    <method name='FocusInId'><arg name='path' direction='in' type='s'/><arg name='client' direction='in' type='s'/></method>"
    "    <method name='FocusOut'/>"
    "    <method name='FocusOutId'><arg name='path' direction='in' type='s'/></method>"
    "    <method name='Reset'/>"
    "    <method name='Enable'/>"
    "    <method name='Disable'/>"
    "    <method name='SetSurroundingText'><arg name='text' direction='in' type='v'/><arg name='cursor' direction='in' type='u'/><arg name='anchor' direction='in' type='u'/></method>"
    "  </interface>"
    "  <interface name='org.freedesktop.IBus.Service'>"
    "    <method name='Destroy'/>"
    "  </interface>"
    "</node>";
  return g_dbus_node_info_new_for_xml (xml, NULL);
}

static const GDBusInterfaceVTable nimf_factory_vtable = {
  nimf_wayland_on_factory_call, NULL, NULL, { 0 }
};

static const GDBusInterfaceVTable nimf_engine_vtable = {
  nimf_wayland_on_engine_call, NULL, NULL, { 0 }
};

static gboolean
nimf_wayland_ibus_try_connect (NimfWayland *wayland)
{
  if (wayland->ibus_conn && !g_dbus_connection_is_closed (wayland->ibus_conn))
    return TRUE;

  nimf_wayland_ibus_stop (wayland);

  gchar *addr = nimf_wayland_find_ibus_address ();
  if (!addr)
    return FALSE;

  wayland->ibus_conn =
    g_dbus_connection_new_for_address_sync (addr,
                                            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                            NULL, NULL, NULL);
  g_free (addr);

  if (!wayland->ibus_conn)
    return FALSE;

  wayland->ibus_engine_path = g_strdup ("/org/freedesktop/IBus/Engine/1");

  GDBusNodeInfo *fnode = nimf_wayland_build_factory_node ();
  GDBusNodeInfo *enode = nimf_wayland_build_engine_node ();

  wayland->ibus_factory_reg_id =
    g_dbus_connection_register_object (wayland->ibus_conn,
                                       "/org/freedesktop/IBus/Factory",
                                       fnode->interfaces[0],
                                       &nimf_factory_vtable,
                                       wayland, NULL, NULL);

  wayland->ibus_engine_reg_id =
    g_dbus_connection_register_object (wayland->ibus_conn,
                                       wayland->ibus_engine_path,
                                       enode->interfaces[0],
                                       &nimf_engine_vtable,
                                       wayland, NULL, NULL);

  g_dbus_node_info_unref (fnode);
  g_dbus_node_info_unref (enode);

  GVariantBuilder d1, d2, op_arr, eng_arr;
  g_variant_builder_init (&d1, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&d2, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&op_arr, G_VARIANT_TYPE ("av"));
  g_variant_builder_init (&eng_arr, G_VARIANT_TYPE ("av"));

  GVariant *edesc = g_variant_new ("(sa{sv}ssssssssussssssss)",
                                   "IBusEngineDesc", &d1,
                                   "nimf", "Nimf",
                                   "Nimf Wayland Input Method",
                                   "ko", "LGPL", "Nimf", "nimf-logo", "us",
                                   (guint32) 99, "", "", "", "", "", "", "", "");
  g_variant_builder_add (&eng_arr, "v", edesc);

  GVariant *comp = g_variant_new ("(sa{sv}ssssssssavav)",
                                  "IBusComponent", &d2,
                                  "org.freedesktop.IBus.Nimf",
                                  "Nimf Wayland Bridge",
                                  PACKAGE_VERSION, "LGPL", "Nimf",
                                  "https://github.com/hamonikr/nimf",
                                  "", "nimf", &op_arr, &eng_arr);

  GVariant *res;
  res = g_dbus_connection_call_sync (wayland->ibus_conn,
                                     "org.freedesktop.IBus",
                                     "/org/freedesktop/IBus",
                                     "org.freedesktop.IBus",
                                     "RegisterComponent",
                                     g_variant_new ("(v)", comp),
                                     NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
  if (res)
    g_variant_unref (res);

  res = g_dbus_connection_call_sync (wayland->ibus_conn,
                                     "org.freedesktop.DBus",
                                     "/org/freedesktop/DBus",
                                     "org.freedesktop.DBus",
                                     "RequestName",
                                     g_variant_new ("(su)", "org.freedesktop.IBus.Nimf", 0),
                                     NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
  if (res)
    g_variant_unref (res);

  wayland->ibus_engine_Changed_sub_id =
    g_dbus_connection_signal_subscribe (wayland->ibus_conn,
                                        NULL,
                                        "org.freedesktop.IBus",
                                        "GlobalEngineChanged",
                                        "/org/freedesktop/IBus",
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        nimf_wayland_on_global_engine_changed,
                                        wayland, NULL);

  nimf_wayland_request_global_engine (wayland);
  return TRUE;
}

static gboolean
nimf_wayland_ibus_on_timer (gpointer user_data)
{
  NimfWayland *wayland = NIMF_WAYLAND (user_data);

  if (!wayland->ibus_conn || g_dbus_connection_is_closed (wayland->ibus_conn))
    nimf_wayland_ibus_try_connect (wayland);
  else
    nimf_wayland_request_global_engine (wayland);

  return G_SOURCE_CONTINUE;
}

static gboolean
nimf_wayland_ibus_start (NimfWayland *wayland)
{
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  if (source)
  {
    GSettingsSchema *schema =
      g_settings_schema_source_lookup (source,
                                       "org.freedesktop.ibus.general.hotkey",
                                       TRUE);
    if (schema)
    {
      const gchar *empty[] = { NULL };
      GSettings *ibus_hotkey = g_settings_new ("org.freedesktop.ibus.general.hotkey");
      if (g_settings_schema_has_key (schema, "trigger"))
        g_settings_set_strv (ibus_hotkey, "trigger", empty);
      if (g_settings_schema_has_key (schema, "triggers"))
        g_settings_set_strv (ibus_hotkey, "triggers", empty);
      g_object_unref (ibus_hotkey);
      g_settings_schema_unref (schema);
    }
  }

  nimf_wayland_ibus_try_connect (wayland);
  wayland->ibus_watch_timer_id =
    g_timeout_add_seconds (3, nimf_wayland_ibus_on_timer, wayland);
  return TRUE;
}

static gboolean nimf_wayland_is_active (NimfService *service)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return NIMF_WAYLAND (service)->active;
}

static gboolean nimf_wayland_start (NimfService *service)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND (service);
  const gchar *type;

  if (wayland->active)
    return TRUE;

  type = g_getenv ("XDG_SESSION_TYPE");

  if (type && g_strcmp0 (type, "wayland"))
    return FALSE;

  wayland->display = wl_display_connect (NULL);
  if (wayland->display == NULL)
  {
    g_warning (G_STRLOC ": %s: wl_display_connect() failed", G_STRFUNC);
    return FALSE;
  }

  wayland->registry = wl_display_get_registry (wayland->display);
  wl_registry_add_listener (wayland->registry, &registry_listener, wayland);
  wl_display_roundtrip (wayland->display);
  if (wayland->input_method == NULL)
  {
    g_message (G_STRLOC ": %s: No zwp_input_method_v1 global; trying IBus/Wayland bridge", G_STRFUNC);
    wayland->wic = nimf_wayland_ic_new (wayland);
    wayland->context = NULL;
    if (nimf_wayland_ibus_start (wayland))
    {
      wayland->active = TRUE;
      return TRUE;
    }
    return FALSE;
  }

  wayland->xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (wayland->xkb_context == NULL) {
    g_critical (G_STRLOC ": %s: xkb_context_new() failed", G_STRFUNC);
    return FALSE;
  }

  wayland->wic = nimf_wayland_ic_new (wayland);
  wayland->context = NULL;
  wayland->event_source = nimf_wayland_source_new (wayland);
  g_source_attach (wayland->event_source, NULL);

  wayland->active = TRUE;

  return TRUE;
}

static const gchar *
nimf_wayland_get_id (NimfService *service)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (NIMF_IS_SERVICE (service), NULL);

  return NIMF_WAYLAND (service)->id;
}

static void
nimf_wayland_init (NimfWayland *wayland)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  wayland->id = g_strdup ("nimf-wayland");
}

static void
nimf_wayland_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND (object);

  if (wayland->active)
    nimf_wayland_stop (NIMF_SERVICE (wayland));

  g_free (wayland->id);

  if (wayland->wic)
    g_object_unref (wayland->wic);

  G_OBJECT_CLASS (nimf_wayland_parent_class)->finalize (object);
}

static void
nimf_wayland_class_init (NimfWaylandClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass     *object_class  = G_OBJECT_CLASS (class);
  NimfServiceClass *service_class = NIMF_SERVICE_CLASS (class);

  service_class->get_id    = nimf_wayland_get_id;
  service_class->start     = nimf_wayland_start;
  service_class->stop      = nimf_wayland_stop;
  service_class->is_active = nimf_wayland_is_active;

  object_class->finalize = nimf_wayland_finalize;
}

static void
nimf_wayland_class_finalize (NimfWaylandClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void module_register_type (GTypeModule *type_module)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_wayland_register_type (type_module);
}

GType module_get_type ()
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return nimf_wayland_get_type ();
}
