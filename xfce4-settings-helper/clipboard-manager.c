/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Matthias Clasen
 * Copyright (C) 2007 Anders Carlsson
 * Copyright (C) 2007 Rodrigo Moya
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2009 Mike Massonnet <mmassonnet@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "utils.h"
#include "clipboard-manager.h"

struct _XfceClipboardManagerClass
{
    GObjectClass __parent__;
};

struct _XfceClipboardManager
{
    GObject __parent__;

    GtkClipboard *default_clipboard;
    GtkClipboard *primary_clipboard;

    GSList       *default_cache;
    gchar        *primary_cache;

    gboolean default_internal_change;
    gboolean primary_timeout;
    gboolean primary_internal_change;
};



G_DEFINE_TYPE (XfceClipboardManager, xfce_clipboard_manager, G_TYPE_OBJECT)



static void
xfce_clipboard_manager_class_init (XfceClipboardManagerClass *klass)
{
}



static void
xfce_clipboard_manager_init (XfceClipboardManager *manager)
{
    manager->default_clipboard =
      gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    manager->primary_clipboard =
      gtk_clipboard_get (GDK_SELECTION_PRIMARY);

    manager->default_cache = NULL;
    manager->primary_cache = NULL;
}



static void
xfce_clipboard_manager_default_store (XfceClipboardManager *manager)
{
    GtkSelectionData *selection_data;
    GdkAtom          *atoms;
    gint              n_atoms;
    gint              i;

    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    if (!gtk_clipboard_wait_for_targets (manager->default_clipboard, &atoms, &n_atoms))
        return;

    if (manager->default_cache != NULL)
    {
        g_slist_foreach (manager->default_cache, (GFunc)gtk_selection_data_free, NULL);
        g_slist_free (manager->default_cache);
        manager->default_cache = NULL;
    }

    for (i = 0; i < n_atoms; i++)
    {
        if (atoms[i] == gdk_atom_intern_static_string ("TARGETS")
            || atoms[i] == gdk_atom_intern_static_string ("MULTIPLE")
            || atoms[i] == gdk_atom_intern_static_string ("DELETE")
            || atoms[i] == gdk_atom_intern_static_string ("INSERT_PROPERTY")
            || atoms[i] == gdk_atom_intern_static_string ("INSERT_SELECTION")
            || atoms[i] == gdk_atom_intern_static_string ("PIXMAP"))
        {
            continue;
        }

        selection_data = gtk_clipboard_wait_for_contents (manager->default_clipboard, atoms[i]);

        if (selection_data == NULL)
            continue;

        manager->default_cache = g_slist_prepend (manager->default_cache, selection_data);
    }
}



static void
xfce_clipboard_manager_default_get_func (GtkClipboard     *clipboard,
                                         GtkSelectionData *selection_data,
                                         guint             info,
                                         gpointer          user_data)
{
    XfceClipboardManager *manager = XFCE_CLIPBOARD_MANAGER (user_data);
    GSList               *list;
    GtkSelectionData     *selection_data_cache = NULL;

    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    list = manager->default_cache;

    for (; list != NULL && list->next != NULL; list = list->next)
    {
        selection_data_cache = list->data;

        if (gtk_selection_data_get_target (selection_data) ==
            gtk_selection_data_get_target (selection_data_cache))
            break;

        selection_data_cache = NULL;
    }

    if (selection_data_cache == NULL)
        return;

    gtk_selection_data_set (selection_data,
                            gtk_selection_data_get_target (selection_data_cache),
                            gtk_selection_data_get_format (selection_data_cache),
                            gtk_selection_data_get_data (selection_data_cache),
                            gtk_selection_data_get_length (selection_data_cache));
}



static void
xfce_clipboard_manager_default_clear_func (GtkClipboard *clipboard,
                                           gpointer      user_data)
{
}



static void
xfce_clipboard_manager_default_restore (XfceClipboardManager *manager)
{
    GtkTargetList    *target_list;
    GtkTargetEntry   *targets;
    gint              n_targets;
    GtkSelectionData *sdata;
    GSList           *list;

    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    list = manager->default_cache;
    if (list == NULL)
      return;

    target_list = gtk_target_list_new (NULL, 0);

    for (; list->next != NULL; list = list->next)
    {
        sdata = list->data;
        gtk_target_list_add (target_list,
                             gtk_selection_data_get_target (sdata),
                             0, 0);
    }

    targets = gtk_target_table_new_from_list (target_list, &n_targets);

    gtk_target_list_unref (target_list);

    gtk_clipboard_set_with_data (manager->default_clipboard,
                                 targets, n_targets,
                                 xfce_clipboard_manager_default_get_func,
                                 xfce_clipboard_manager_default_clear_func,
                                 manager);
}



static void
xfce_clipboard_manager_default_owner_change (XfceClipboardManager *manager,
                                             GdkEventOwnerChange  *event)
{
    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    if (event->send_event == TRUE)
      return;

    if (event->owner != 0)
    {
        if (manager->default_internal_change)
        {
            manager->default_internal_change = FALSE;
            return;
        }

        xfce_clipboard_manager_default_store (manager);
    }
    else
    {
        /* This 'bug' happens with Mozilla applications, it means that
         * we restored the clipboard (we own it now) but somehow we are
         * being noticed twice about that fact where first the owner is
         * 0 (which is when we must restore) but then again where the
         * owner is ourself (which is what normally only happens and
         * also that means that we have to store the clipboard content
         * e.g. owner is not 0). By the second time we would store
         * ourself back with an empty clipboard... solution is to jump
         * over the first time and don't try to restore empty data. */
        if (manager->default_internal_change)
            return;

        manager->default_internal_change = TRUE;
        xfce_clipboard_manager_default_restore (manager);
    }
}



static gboolean
xfce_clipboard_manager_primary_clipboard_store (XfceClipboardManager *manager)
{
    GdkModifierType state;
    gchar *text;

    gdk_window_get_pointer (NULL, NULL, NULL, &state);

    if (state & (GDK_BUTTON1_MASK|GDK_SHIFT_MASK))
        return TRUE;

    text = gtk_clipboard_wait_for_text (manager->primary_clipboard);

    if (text != NULL)
    {
        g_free (manager->primary_cache);
        manager->primary_cache = text;
    }

    manager->primary_timeout = 0;

    return FALSE;
}


static void
xfce_clipboard_manager_primary_owner_change (XfceClipboardManager *manager,
                                             GdkEventOwnerChange *event)
{
    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    if (event->send_event == TRUE)
        return;

    if (event->owner != 0)
    {
        if (manager->primary_internal_change == TRUE)
        {
            manager->primary_internal_change = FALSE;
            return;
        }

        if (manager->primary_timeout == 0)
        {
            manager->primary_timeout =
            g_timeout_add (250, (GSourceFunc)xfce_clipboard_manager_primary_clipboard_store, manager);
        }
    }
    else
    {
        if (manager->primary_cache != NULL)
            gtk_clipboard_set_text (manager->primary_clipboard,
                                    manager->primary_cache,
                                    -1);
        manager->primary_internal_change = TRUE;
    }
}



static gboolean
xfce_clipboard_manager_selection_owner (const gchar   *selection_name,
                                        gboolean       force,
                                        GdkFilterFunc  filter_func)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *gdpy = gdk_display_get_default ();
    GtkWidget *invisible;
    Display *dpy = GDK_DISPLAY_XDISPLAY (gdpy);
    GdkWindow *rootwin = gdk_screen_get_root_window (gdk_display_get_screen (gdpy, 0));
    GdkWindow *invisible_window;
    Window xroot = GDK_WINDOW_XID (rootwin);
    GdkAtom selection_atom;
    Atom selection_atom_x11;
    XClientMessageEvent xev;
    gboolean has_owner;

    g_return_val_if_fail (selection_name != NULL, FALSE);

    selection_atom = gdk_atom_intern (selection_name, FALSE);
    selection_atom_x11 = gdk_x11_atom_to_xatom_for_display (gdpy, selection_atom);

    /* can't use gdk for the selection owner here because it returns NULL
     * if the selection owner is in another process */
    if (!force)
    {
        gdk_error_trap_push ();
        has_owner = XGetSelectionOwner (dpy, selection_atom_x11) != None;
        gdk_flush ();
        gdk_error_trap_pop ();
        if (has_owner)
            return FALSE;
    }

    invisible = gtk_invisible_new ();
    gtk_widget_realize (invisible);
    gtk_widget_add_events (invisible, GDK_STRUCTURE_MASK | GDK_PROPERTY_CHANGE_MASK);

    invisible_window = gtk_widget_get_window (invisible);

    if (!gdk_selection_owner_set_for_display (gdpy, invisible_window,
                                              selection_atom, GDK_CURRENT_TIME,
                                              TRUE))
    {
        g_critical ("Unable to get selection %s", selection_name);
        gtk_widget_destroy (invisible);
        return FALSE;
    }

    /* but we can use gdk here since we only care if it's our window */
    if (gdk_selection_owner_get_for_display (gdpy, selection_atom) != invisible_window)
    {
        gtk_widget_destroy (invisible);
        return FALSE;
    }

    xev.type = ClientMessage;
    xev.window = xroot;
    xev.message_type = gdk_x11_get_xatom_by_name_for_display (gdpy, "MANAGER");
    xev.format = 32;
    xev.data.l[0] = CurrentTime;
    xev.data.l[1] = selection_atom_x11;
    xev.data.l[2] = GDK_WINDOW_XID (invisible_window);
    xev.data.l[3] = xev.data.l[4] = 0;

    gdk_error_trap_push ();
    XSendEvent (dpy, xroot, False, StructureNotifyMask, (XEvent *)&xev);
    gdk_flush ();
    if (gdk_error_trap_pop ())
      g_critical ("Failed to send client event");

    if (filter_func != NULL)
        gdk_window_add_filter (invisible_window, filter_func, invisible);
#endif

    return TRUE;
}



XfceClipboardManager *
xfce_clipboard_manager_new (void)
{
   return g_object_new (XFCE_TYPE_CLIPBOARD_MANAGER, NULL);
}



gboolean
xfce_clipboard_manager_start (XfceClipboardManager *manager)
{
    g_return_val_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager), FALSE);

    /* Check if there is a clipboard manager running */
    if (gdk_display_supports_clipboard_persistence (gdk_display_get_default ()))
    {
        g_message ("A clipboard manager is already running.");
        return FALSE;
    }

    if (!xfce_clipboard_manager_selection_owner ("CLIPBOARD_MANAGER", FALSE, NULL))
    {
        g_warning ("Unable to get the clipboard manager selection.");
        return FALSE;
    }

    g_signal_connect_swapped (manager->default_clipboard, "owner-change",
                              G_CALLBACK (xfce_clipboard_manager_default_owner_change),
                              manager);
    g_signal_connect_swapped (manager->primary_clipboard, "owner-change",
                              G_CALLBACK (xfce_clipboard_manager_primary_owner_change),
                              manager);

    return TRUE;
}



void
xfce_clipboard_manager_stop (XfceClipboardManager *manager)
{
    g_return_if_fail (XFCE_IS_CLIPBOARD_MANAGER (manager));

    g_signal_handlers_disconnect_by_func (manager->default_clipboard,
                                          xfce_clipboard_manager_default_owner_change,
                                          manager);
    g_signal_handlers_disconnect_by_func (manager->primary_clipboard,
                                          xfce_clipboard_manager_primary_owner_change,
                                          manager);

    if (manager->default_cache != NULL)
    {
        g_slist_foreach (manager->default_cache, (GFunc)gtk_selection_data_free, NULL);
        g_slist_free (manager->default_cache);
        manager->default_cache = NULL;
    }

    if (manager->primary_cache != NULL)
        g_free (manager->primary_cache);
}
