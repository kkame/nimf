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

  g_variant_builder_init (&attr_list_builder, G_VARIANT_TYPE ("av"));

  if (attrs)
  {
    gint i;
    for (i = 0; attrs[i] != NULL; i++)
    {
      GVariantBuilder attr_dict;
      guint32 attr_type = 1; /* IBUS_ATTR_TYPE_UNDERLINE */
      guint32 attr_val  = 1; /* IBUS_ATTR_UNDERLINE_SINGLE */

      if (attrs[i]->type == NIMF_PREEDIT_ATTR_HIGHLIGHT)
      {
        attr_type = 3; /* IBUS_ATTR_TYPE_BACKGROUND */
        attr_val  = 0x00ffff00;
      }

      g_variant_builder_init (&attr_dict, G_VARIANT_TYPE ("a{sv}"));
      GVariant *attr_var = g_variant_new ("(sa{sv}uuuu)",
                                          "IBusAttribute",
                                          &attr_dict,
                                          attr_type,
                                          attr_val,
                                          (guint32) attrs[i]->start_index,
                                          (guint32) attrs[i]->end_index);
      g_variant_builder_add (&attr_list_builder, "v", attr_var);
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

void
nimf_wayland_ic_emit_commit (NimfServiceIC *ic,
                             const gchar   *text)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->context)
  {
    zwp_input_method_context_v1_commit_string (wayland->context,
                                               wayland->serial,
                                               text);
  }

  if (wayland->ibus_conn && wayland->ibus_engine_path)
  {
    GVariant *ibus_text = nimf_wayland_ic_build_ibus_text (text, NULL);
    g_dbus_connection_emit_signal (wayland->ibus_conn,
                                   NULL,
                                   wayland->ibus_engine_path,
                                   "org.freedesktop.IBus.Engine",
                                   "CommitText",
                                   g_variant_new ("(v)", ibus_text),
                                   NULL);
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

  if (wayland->context)
  {
    zwp_input_method_context_v1_preedit_cursor (wayland->context, cursor_pos);
    zwp_input_method_context_v1_preedit_string (wayland->context,
                 wayland->serial, preedit_string, preedit_string);
  }

  if (wayland->ibus_conn && wayland->ibus_engine_path)
  {
    gboolean visible = (preedit_string && preedit_string[0] != '\0');
    GVariant *ibus_text = nimf_wayland_ic_build_ibus_text (preedit_string, attrs);
    g_dbus_connection_emit_signal (wayland->ibus_conn,
                                   NULL,
                                   wayland->ibus_engine_path,
                                   "org.freedesktop.IBus.Engine",
                                   "UpdatePreeditText",
                                   g_variant_new ("(vubu)",
                                                  ibus_text,
                                                  (guint32) (cursor_pos >= 0 ? cursor_pos : 0),
                                                  visible,
                                                  (guint32) 0),
                                   NULL);
  }
}

void nimf_wayland_ic_emit_preedit_end (NimfServiceIC *ic)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfWayland *wayland = NIMF_WAYLAND_IC (ic)->wayland;

  if (wayland->ibus_conn && wayland->ibus_engine_path)
  {
    GVariant *ibus_text = nimf_wayland_ic_build_ibus_text ("", NULL);
    g_dbus_connection_emit_signal (wayland->ibus_conn,
                                   NULL,
                                   wayland->ibus_engine_path,
                                   "org.freedesktop.IBus.Engine",
                                   "UpdatePreeditText",
                                   g_variant_new ("(vubu)",
                                                  ibus_text,
                                                  (guint32) 0,
                                                  FALSE,
                                                  (guint32) 0),
                                   NULL);
  }
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

  service_im_class->emit_commit          = nimf_wayland_ic_emit_commit;
  service_im_class->emit_preedit_start   = nimf_wayland_ic_emit_preedit_start;
  service_im_class->emit_preedit_changed = nimf_wayland_ic_emit_preedit_changed;
  service_im_class->emit_preedit_end     = nimf_wayland_ic_emit_preedit_end;
  service_im_class->get_service_id       = nimf_wayland_ic_get_service_id;
}
