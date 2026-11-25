/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/*
 * nimf-wayland-ic.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2017-2019 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nimf-wayland-ic.h"
#include "input-method-unstable-v1-client-protocol.h"

G_DEFINE_TYPE (NimfWaylandIC, nimf_wayland_ic, NIMF_TYPE_SERVICE_IC);

NimfWaylandIC *nimf_wayland_ic_new (NimfWayland *wayland)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWaylandIC *wic;

  wic = g_object_new (NIMF_TYPE_WAYLAND_IC, NULL);
  wic->wayland = wayland;

  return wic;
}

static GVariant *
nimf_wayland_ic_build_ibus_text (const gchar      *text,
                                 NimfPreeditAttr **attrs)
{
  GVariantBuilder attr_list_builder;
  GVariantBuilder empty_dict;
  guint32         char_len = text ? (guint32) g_utf8_strlen (text, -1) : 0;

  g_variant_builder_init (&attr_list_builder, G_VARIANT_TYPE ("av"));

  if (attrs && char_len > 0)
  {
    gint i;
    for (i = 0; attrs[i] != NULL; i++)
    {
      GVariantBuilder attr_dict;
      guint32 attr_type = 1; /* IBUS_ATTR_TYPE_UNDERLINE */
      guint32 attr_val  = 1; /* IBUS_ATTR_UNDERLINE_SINGLE */
      guint32 hint_val  = 1; /* IBUS_ATTR_PREEDIT_WHOLE */
      guint32 start_idx = (guint32) attrs[i]->start_index;
      guint32 end_idx   = (guint32) attrs[i]->end_index;

      if (start_idx > char_len)
        start_idx = char_len;
      if (end_idx > char_len)
        end_idx = char_len;
      if (end_idx <= start_idx)
        continue;

      if (attrs[i]->type == NIMF_PREEDIT_ATTR_HIGHLIGHT)
      {
        attr_type = 3; /* IBUS_ATTR_TYPE_BACKGROUND */
        attr_val  = 0x00ffff00;
        hint_val  = 2; /* IBUS_ATTR_PREEDIT_SELECTION */
      }

      g_variant_builder_init (&attr_dict, G_VARIANT_TYPE ("a{sv}"));
      GVariant *attr_var = g_variant_new ("(sa{sv}uuuu)",
                                          "IBusAttribute",
                                          &attr_dict,
                                          attr_type,
                                          attr_val,
                                          start_idx,
                                          end_idx);
      g_variant_builder_add (&attr_list_builder, "v", attr_var);

      /* GNOME Shell / Mutter reads IBUS_ATTR_TYPE_HINT (4) */
      g_variant_builder_init (&attr_dict, G_VARIANT_TYPE ("a{sv}"));
      GVariant *hint_var = g_variant_new ("(sa{sv}uuuu)",
                                          "IBusAttribute",
                                          &attr_dict,
                                          (guint32) 4, /* IBUS_ATTR_TYPE_HINT */
                                          hint_val,
                                          start_idx,
                                          end_idx);
      g_variant_builder_add (&attr_list_builder, "v", hint_var);
    }
  }

  g_variant_builder_init (&empty_dict, G_VARIANT_TYPE ("a{sv}"));
  GVariant *attr_list_var = g_variant_new ("(sa{sv}av)",
                                           "IBusAttrList",
                                           &empty_dict,
                                           &attr_list_builder);

  g_variant_builder_init (&empty_dict, G_VARIANT_TYPE ("a{sv}"));
  return g_variant_new ("(sa{sv}sv)",
                        "IBusText",
                        &empty_dict,
                        text ? text : "",
                        attr_list_var);
}

static void
nimf_wayland_ic_ibus_send_preedit (NimfWayland      *wayland,
                                   const gchar      *preedit_string,
                                   NimfPreeditAttr **attrs,
                                   gint              cursor_pos)
{
  if (!wayland->ibus_conn || !wayland->ibus_engine_path)
    return;

  gboolean visible = (preedit_string && preedit_string[0] != '\0');

  if (!visible && !wayland->ibus_preedit_visible)
    return;

  /* Use IBUS_ENGINE_PREEDIT_COMMIT (1) while composing so Mutter/IBus commits
   * the composing syllable synchronously on mouse click or focus-out before
   * moving the cursor or switching focus. */
  guint32 mode = visible ? (guint32) 1 : (guint32) 0;
  GVariant *ibus_text = nimf_wayland_ic_build_ibus_text (visible ? preedit_string : "",
                                                         visible ? attrs : NULL);
  g_dbus_connection_emit_signal (wayland->ibus_conn,
                                 NULL,
                                 wayland->ibus_engine_path,
                                 "org.freedesktop.IBus.Engine",
                                 "UpdatePreeditText",
                                 g_variant_new ("(vubu)",
                                                ibus_text,
                                                (guint32) (visible && cursor_pos >= 0 ? cursor_pos : 0),
                                                visible,
                                                mode),
                                 NULL);
  wayland->ibus_preedit_visible = visible;
}

static void
nimf_wayland_ic_ibus_send_commit (NimfWayland *wayland,
                                  const gchar *text)
{
  if (!wayland->ibus_conn || !wayland->ibus_engine_path || !text || text[0] == '\0')
    return;

  /* Always clear preedit before committing (matches ibus-hangul / ibus-daemon) */
  if (wayland->ibus_preedit_visible)
    nimf_wayland_ic_ibus_send_preedit (wayland, "", NULL, 0);

  GVariant *ibus_text = nimf_wayland_ic_build_ibus_text (text, NULL);
  g_dbus_connection_emit_signal (wayland->ibus_conn,
                                 NULL,
                                 wayland->ibus_engine_path,
                                 "org.freedesktop.IBus.Engine",
                                 "CommitText",
                                 g_variant_new ("(v)", ibus_text),
                                 NULL);
}

void
nimf_wayland_ic_ibus_flush_buffered (NimfWayland *wayland,
                                     gboolean    *handled,
                                     guint32      keyval,
                                     guint32      keycode,
                                     guint32      state,
                                     gboolean     is_key_press)
{
  gboolean had_commit = (wayland->ibus_pending_commit &&
                         wayland->ibus_pending_commit->len > 0);
  gboolean has_new_preedit = (wayland->ibus_has_pending_preedit &&
                              wayland->ibus_pending_preedit &&
                              wayland->ibus_pending_preedit[0] != '\0');

  /* When a non-handled printable ASCII key (such as Space, digits, or shifted
   * punctuation) flushes the composing Hangul syllable, append it to the same
   * atomic CommitText so Wayland zwp_text_input_v3 cannot race with
   * wl_keyboard.key or overwrite the first commit_string before .done. */
  if (had_commit && !(*handled) && is_key_press && !has_new_preedit)
  {
    guint32 non_shift_mods = state & (NIMF_CONTROL_MASK | NIMF_MOD1_MASK |
                                      NIMF_MOD4_MASK | NIMF_SUPER_MASK |
                                      NIMF_HYPER_MASK | NIMF_META_MASK);
    gboolean is_plain_space = (keyval == 0x20 && !(state & NIMF_SHIFT_MASK));
    gboolean is_printable   = (keyval >= 0x21 && keyval <= 0x7e);

    if (non_shift_mods == 0 && (is_plain_space || is_printable))
    {
      g_string_append_c (wayland->ibus_pending_commit, (gchar) keyval);
      *handled = TRUE;
    }
  }

  if (had_commit)
  {
    nimf_wayland_ic_ibus_send_commit (wayland, wayland->ibus_pending_commit->str);
    g_string_truncate (wayland->ibus_pending_commit, 0);
  }

  if (wayland->ibus_has_pending_preedit)
  {
    nimf_wayland_ic_ibus_send_preedit (wayland,
                                       wayland->ibus_pending_preedit,
                                       wayland->ibus_pending_preedit_attrs,
                                       wayland->ibus_pending_cursor_pos);
    wayland->ibus_has_pending_preedit = FALSE;
    g_clear_pointer (&wayland->ibus_pending_preedit, g_free);
    nimf_preedit_attr_freev (wayland->ibus_pending_preedit_attrs);
    wayland->ibus_pending_preedit_attrs = NULL;
    wayland->ibus_pending_cursor_pos = 0;
  }

  /* For synchronous IBus clients (IBUS_CAP_SYNC_PROCESS_KEY = 0x80), forward
   * the unhandled key after CommitText so event order is strictly preserved. */
  if (had_commit && !(*handled) && is_key_press &&
      (wayland->ibus_caps & 0x80) &&
      wayland->ibus_conn && wayland->ibus_engine_path)
  {
    g_dbus_connection_emit_signal (wayland->ibus_conn,
                                   NULL,
                                   wayland->ibus_engine_path,
                                   "org.freedesktop.IBus.Engine",
                                   "ForwardKeyEvent",
                                   g_variant_new ("(uuu)", keyval, keycode, state),
                                   NULL);
    *handled = TRUE;
  }
}

void
nimf_wayland_ic_emit_commit (NimfServiceIC *ic,
                             const gchar   *text)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->context && !wayland->password_field)
  {
    zwp_input_method_context_v1_commit_string (wayland->context,
                                               wayland->serial,
                                               text);
  }

  if (wayland->ibus_conn && wayland->ibus_engine_path &&
      !wayland->ibus_suppress_signals && !wayland->ibus_password_field)
  {
    if (wayland->ibus_buffering)
    {
      if (text && text[0] != '\0')
        g_string_append (wayland->ibus_pending_commit, text);
    }
    else
    {
      nimf_wayland_ic_ibus_send_commit (wayland, text);
    }
  }
}

void nimf_wayland_ic_emit_preedit_start (NimfServiceIC *ic)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void
nimf_wayland_ic_emit_preedit_changed (NimfServiceIC    *ic,
                                      const gchar      *preedit_string,
                                      NimfPreeditAttr **attrs,
                                      gint              cursor_pos)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->context && !wayland->password_field)
  {
    zwp_input_method_context_v1_preedit_cursor (wayland->context, cursor_pos);
    zwp_input_method_context_v1_preedit_string (wayland->context,
                 wayland->serial, preedit_string, preedit_string);
  }

  if (wayland->ibus_conn && wayland->ibus_engine_path &&
      !wayland->ibus_suppress_signals && !wayland->ibus_password_field)
  {
    if (wayland->ibus_buffering)
    {
      g_free (wayland->ibus_pending_preedit);
      nimf_preedit_attr_freev (wayland->ibus_pending_preedit_attrs);
      wayland->ibus_pending_preedit       = g_strdup (preedit_string ? preedit_string : "");
      wayland->ibus_pending_preedit_attrs = nimf_preedit_attrs_copy (attrs);
      wayland->ibus_pending_cursor_pos    = cursor_pos;
      wayland->ibus_has_pending_preedit   = TRUE;
    }
    else
    {
      nimf_wayland_ic_ibus_send_preedit (wayland, preedit_string, attrs, cursor_pos);
    }
  }
}

void nimf_wayland_ic_emit_preedit_end (NimfServiceIC *ic)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->ibus_conn && wayland->ibus_engine_path &&
      !wayland->ibus_suppress_signals && !wayland->ibus_password_field)
  {
    if (wayland->ibus_buffering)
    {
      if (!wayland->ibus_has_pending_preedit)
      {
        g_free (wayland->ibus_pending_preedit);
        nimf_preedit_attr_freev (wayland->ibus_pending_preedit_attrs);
        wayland->ibus_pending_preedit       = g_strdup ("");
        wayland->ibus_pending_preedit_attrs = NULL;
        wayland->ibus_pending_cursor_pos    = 0;
        wayland->ibus_has_pending_preedit   = TRUE;
      }
    }
    else
    {
      nimf_wayland_ic_ibus_send_preedit (wayland, "", NULL, 0);
    }
  }
}

static gboolean
nimf_wayland_ic_emit_delete_surrounding (NimfServiceIC *ic,
                                         gint           offset,
                                         gint           n_chars)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->ibus_conn && wayland->ibus_engine_path &&
      !wayland->ibus_suppress_signals && !wayland->ibus_password_field)
  {
    g_dbus_connection_emit_signal (wayland->ibus_conn,
                                   NULL,
                                   wayland->ibus_engine_path,
                                   "org.freedesktop.IBus.Engine",
                                   "DeleteSurroundingText",
                                   g_variant_new ("(iu)",
                                                  (gint32) offset,
                                                  (guint32) (n_chars > 0 ? n_chars : 0)),
                                   NULL);
    return TRUE;
  }

  return FALSE;
}

static const gchar *
nimf_wayland_ic_get_service_id (NimfServiceIC *ic)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return nimf_service_get_id (NIMF_SERVICE (NIMF_WAYLAND_IC (ic)->wayland));
}

static void
nimf_wayland_ic_init (NimfWaylandIC *wic)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static void
nimf_wayland_ic_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  G_OBJECT_CLASS (nimf_wayland_ic_parent_class)->finalize (object);
}

static void
nimf_wayland_ic_class_init (NimfWaylandICClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass       *object_class     = G_OBJECT_CLASS (class);
  NimfServiceICClass *service_im_class = NIMF_SERVICE_IC_CLASS (class);

  object_class->finalize = nimf_wayland_ic_finalize;

  service_im_class->emit_commit             = nimf_wayland_ic_emit_commit;
  service_im_class->emit_preedit_start      = nimf_wayland_ic_emit_preedit_start;
  service_im_class->emit_preedit_changed    = nimf_wayland_ic_emit_preedit_changed;
  service_im_class->emit_preedit_end        = nimf_wayland_ic_emit_preedit_end;
  service_im_class->emit_delete_surrounding = nimf_wayland_ic_emit_delete_surrounding;
  service_im_class->get_service_id          = nimf_wayland_ic_get_service_id;
}
