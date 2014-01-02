/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 *
 */

#include "config.h"

#include <string.h>
#include <strings.h>
#include <glib.h>

#include "nd-notification.h"
#include "nd-bubble.h"

#define ND_BUBBLE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ND_TYPE_BUBBLE, NdBubblePrivate))

#define TIMEOUT_SEC   5

#define WIDTH         400
#define DEFAULT_X0    0
#define DEFAULT_Y0    0
#define DEFAULT_RADIUS 16
#define IMAGE_SIZE    48
#define BODY_X_OFFSET (IMAGE_SIZE + 8)
#define BACKGROUND_ALPHA    0.90

#define MAX_ICON_SIZE IMAGE_SIZE

struct NdBubblePrivate
{
        NdNotification *notification;

        GtkWidget      *main_hbox;
        GtkWidget      *iconbox;
        GtkWidget      *icon;
        GtkWidget      *content_hbox;
        GtkWidget      *summary_label;
        GtkWidget      *close_button;
        GtkWidget      *body_label;
        GtkWidget      *actions_box;
        GtkWidget      *last_sep;

        int             width;
        int             height;
        int             last_width;
        int             last_height;

        gboolean        have_icon;
        gboolean        have_body;
        gboolean        have_actions;

        gboolean        url_clicked_lock;

        gboolean        composited;
        glong           remaining;
        guint           timeout_id;
};

static void     nd_bubble_class_init  (NdBubbleClass *klass);
static void     nd_bubble_init        (NdBubble      *bubble);
static void     nd_bubble_finalize    (GObject       *object);
static void     on_notification_changed (NdNotification *notification,
                                         NdBubble       *bubble);

G_DEFINE_TYPE (NdBubble, nd_bubble, GTK_TYPE_WINDOW)

NdNotification *
nd_bubble_get_notification (NdBubble *bubble)
{
        g_return_val_if_fail (ND_IS_BUBBLE (bubble), NULL);

        return bubble->priv->notification;
}

static gboolean
nd_bubble_configure_event (GtkWidget         *widget,
                           GdkEventConfigure *event)
{
        NdBubble *bubble = ND_BUBBLE (widget);

        bubble->priv->width = event->width;
        bubble->priv->height = event->height;

        gtk_widget_queue_draw (widget);

        return FALSE;
}

static void
color_reverse (const GdkColor *a,
               GdkColor       *b)
{
        gdouble red;
        gdouble green;
        gdouble blue;
        gdouble h;
        gdouble s;
        gdouble v;

        red = (gdouble) a->red / 65535.0;
        green = (gdouble) a->green / 65535.0;
        blue = (gdouble) a->blue / 65535.0;

        gtk_rgb_to_hsv (red, green, blue, &h, &s, &v);

        /* pivot brightness around the center */
        v = 0.5 + (0.5 - v);
        if (v > 1.0)
                v = 1.0;
        else if (v < 0.0)
                v = 0.0;

        /* reduce saturation by 50% */
        s *= 0.5;

        gtk_hsv_to_rgb (h, s, v, &red, &green, &blue);

        b->red = red * 65535.0;
        b->green = green * 65535.0;
        b->blue = blue * 65535.0;
}

static void
override_style (GtkWidget *widget,
                GtkStyle  *previous_style)
{
        GtkStateType state;
        GtkStyle    *style;
        GtkStyle    *old_style;
        GdkColor     fg;
        GdkColor     bg;

        gtk_widget_ensure_style (widget);
        old_style = gtk_widget_get_style (widget);
        style = gtk_style_copy (old_style);
        if (previous_style == NULL
            || (previous_style != NULL
                && (previous_style->bg[GTK_STATE_NORMAL].red != style->bg[GTK_STATE_NORMAL].red
                    || previous_style->bg[GTK_STATE_NORMAL].green != style->bg[GTK_STATE_NORMAL].green
                    || previous_style->bg[GTK_STATE_NORMAL].blue != style->bg[GTK_STATE_NORMAL].blue))) {

                state = (GtkStateType) 0;
                while (state < (GtkStateType) G_N_ELEMENTS (old_style->bg))  {
                        color_reverse (&style->bg[state], &bg);
                        gtk_widget_modify_bg (widget, state, &bg);
                        state++;
                }

        }

        if (previous_style == NULL
            || (previous_style != NULL
                && (previous_style->fg[GTK_STATE_NORMAL].red != style->fg[GTK_STATE_NORMAL].red
                    || previous_style->fg[GTK_STATE_NORMAL].green != style->fg[GTK_STATE_NORMAL].green
                    || previous_style->fg[GTK_STATE_NORMAL].blue != style->fg[GTK_STATE_NORMAL].blue))) {

                state = (GtkStateType) 0;
                while (state < (GtkStateType) G_N_ELEMENTS (old_style->fg)) {
                        color_reverse (&style->fg[state], &fg);
                        gtk_widget_modify_fg (widget, state, &fg);
                        state++;
                }
        }

        g_object_unref (style);
}

static void
nd_bubble_composited_changed (GtkWidget *widget)
{
        NdBubble  *bubble = ND_BUBBLE (widget);
        GdkScreen *screen;
        GdkVisual *visual;

        bubble->priv->composited = gdk_screen_is_composited (gtk_widget_get_screen (widget));

        screen = gtk_window_get_screen (GTK_WINDOW (bubble));
        visual = gdk_screen_get_rgba_visual (screen);
        if (visual == NULL) {
                visual = gdk_screen_get_system_visual (screen);
         }

        gtk_widget_set_visual (GTK_WIDGET (bubble), visual);

        gtk_widget_queue_draw (widget);
}

static void
draw_round_rect (cairo_t *cr,
                 gdouble  aspect,
                 gdouble  x,
                 gdouble  y,
                 gdouble  corner_radius,
                 gdouble  width,
                 gdouble  height)
{
        gdouble radius = corner_radius / aspect;

        cairo_move_to (cr, x + radius, y);

        // top-right, left of the corner
        cairo_line_to (cr,
                       x + width - radius,
                       y);

        // top-right, below the corner
        cairo_arc (cr,
                   x + width - radius,
                   y + radius,
                   radius,
                   -90.0f * G_PI / 180.0f,
                   0.0f * G_PI / 180.0f);

        // bottom-right, above the corner
        cairo_line_to (cr,
                       x + width,
                       y + height - radius);

        // bottom-right, left of the corner
        cairo_arc (cr,
                   x + width - radius,
                   y + height - radius,
                   radius,
                   0.0f * G_PI / 180.0f,
                   90.0f * G_PI / 180.0f);

        // bottom-left, right of the corner
        cairo_line_to (cr,
                       x + radius,
                       y + height);

        // bottom-left, above the corner
        cairo_arc (cr,
                   x + radius,
                   y + height - radius,
                   radius,
                   90.0f * G_PI / 180.0f,
                   180.0f * G_PI / 180.0f);

        // top-left, below the corner
        cairo_line_to (cr,
                       x,
                       y + radius);

        // top-left, right of the corner
        cairo_arc (cr,
                   x + radius,
                   y + radius,
                   radius,
                   180.0f * G_PI / 180.0f,
                   270.0f * G_PI / 180.0f);
}

static void
paint_bubble (NdBubble *bubble,
              cairo_t  *cr)
{
        GdkColor         color;
        double           r, g, b;
        cairo_t         *cr2;
        cairo_surface_t *surface;
        cairo_region_t  *region;
        GtkStyle        *style;
        GtkAllocation    allocation;

        gtk_widget_get_allocation (GTK_WIDGET (bubble), &allocation);
        if (bubble->priv->width == 0 || bubble->priv->height == 0) {
                bubble->priv->width = MAX (allocation.width, 1);
                bubble->priv->height = MAX (allocation.height, 1);
        }

        surface = cairo_surface_create_similar (cairo_get_target (cr),
                                                CAIRO_CONTENT_COLOR_ALPHA,
                                                bubble->priv->width,
                                                bubble->priv->height);
        cr2 = cairo_create (surface);

        /* transparent background */
        cairo_rectangle (cr2, 0, 0, bubble->priv->width, bubble->priv->height);
        cairo_set_source_rgba (cr2, 0.0, 0.0, 0.0, 0.0);
        cairo_fill (cr2);

        draw_round_rect (cr2,
                         1.0f,
                         DEFAULT_X0 + 1,
                         DEFAULT_Y0 + 1,
                         DEFAULT_RADIUS,
                         allocation.width - 2,
                         allocation.height - 2);

        style = gtk_widget_get_style (GTK_WIDGET (bubble));
        color = style->bg [GTK_STATE_NORMAL];
        r = (float)color.red / 65535.0;
        g = (float)color.green / 65535.0;
        b = (float)color.blue / 65535.0;
        cairo_set_source_rgba (cr2, r, g, b, BACKGROUND_ALPHA);
        cairo_fill_preserve (cr2);

        color = style->text_aa [GTK_STATE_NORMAL];
        r = (float) color.red / 65535.0;
        g = (float) color.green / 65535.0;
        b = (float) color.blue / 65535.0;
        cairo_set_source_rgba (cr2, r, g, b, BACKGROUND_ALPHA / 2);
        cairo_set_line_width (cr2, 2);
        cairo_stroke (cr2);

        cairo_destroy (cr2);

        cairo_save (cr);
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface (cr, surface, 0, 0);
        cairo_paint (cr);
        cairo_restore (cr);

        if (bubble->priv->width == bubble->priv->last_width
            && bubble->priv->height == bubble->priv->last_height) {
                goto done;
        }

        /* Don't shape when composited */
        if (bubble->priv->composited) {
                gtk_widget_shape_combine_region (GTK_WIDGET (bubble), NULL);
                goto done;
        }

        bubble->priv->last_width = bubble->priv->width;
        bubble->priv->last_height = bubble->priv->height;

        region = gdk_cairo_region_create_from_surface (surface);
        gtk_widget_shape_combine_region (GTK_WIDGET (bubble), region);
        cairo_region_destroy (region);

 done:
        cairo_surface_destroy (surface);

}

static gboolean
nd_bubble_draw (GtkWidget *widget,
                cairo_t   *cr)
{
        NdBubble *bubble = ND_BUBBLE (widget);

        paint_bubble (bubble, cr);

        GTK_WIDGET_CLASS (nd_bubble_parent_class)->draw (widget, cr);

        return FALSE;
}

static gboolean
nd_bubble_button_release_event (GtkWidget      *widget,
                                GdkEventButton *event)
{
        NdBubble *bubble = ND_BUBBLE (widget);

        if (bubble->priv->url_clicked_lock) {
                bubble->priv->url_clicked_lock = FALSE;
                return FALSE;
        }

        nd_notification_action_invoked (bubble->priv->notification, "default");
        gtk_widget_destroy (GTK_WIDGET (bubble));

        return FALSE;
}

static gboolean
timeout_bubble (NdBubble *bubble)
{
        bubble->priv->timeout_id = 0;

        /* FIXME: if transient also close it */

        gtk_widget_destroy (GTK_WIDGET (bubble));

        return FALSE;
}

static void
add_timeout (NdBubble *bubble)
{
        if (bubble->priv->timeout_id != 0) {
                g_source_remove (bubble->priv->timeout_id);
        }
        bubble->priv->timeout_id = g_timeout_add_seconds (TIMEOUT_SEC,
                                                          (GSourceFunc)timeout_bubble,
                                                          bubble);
}

static void
nd_bubble_realize (GtkWidget *widget)
{
        NdBubble *bubble = ND_BUBBLE (widget);

        add_timeout (bubble);

        GTK_WIDGET_CLASS (nd_bubble_parent_class)->realize (widget);
}

static gboolean
nd_bubble_enter_notify_event (GtkWidget        *widget,
                              GdkEventCrossing *event)
{
        NdBubble *bubble = ND_BUBBLE (widget);
        if (bubble->priv->timeout_id != 0) {
                g_source_remove (bubble->priv->timeout_id);
        }

        return FALSE;
}

static gboolean
nd_bubble_leave_notify_event (GtkWidget        *widget,
                              GdkEventCrossing *event)
{
        NdBubble *bubble = ND_BUBBLE (widget);

        add_timeout (bubble);
        return FALSE;
}

static void
nd_bubble_class_init (NdBubbleClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = nd_bubble_finalize;

        widget_class->draw = nd_bubble_draw;
        widget_class->configure_event = nd_bubble_configure_event;
        widget_class->composited_changed = nd_bubble_composited_changed;
        widget_class->button_release_event = nd_bubble_button_release_event;
        widget_class->enter_notify_event = nd_bubble_enter_notify_event;
        widget_class->leave_notify_event = nd_bubble_leave_notify_event;
        widget_class->realize = nd_bubble_realize;

        g_type_class_add_private (klass, sizeof (NdBubblePrivate));
}

static gboolean
on_activate_link (GtkLabel *label,
                  char     *uri,
                  NdBubble *bubble)
{
        char *escaped_uri;
        char *cmd = NULL;

        /* Somewhat of a hack.. */
        bubble->priv->url_clicked_lock = TRUE;

        escaped_uri = g_shell_quote (uri);

        if (g_find_program_in_path ("gvfs-open") != NULL) {
                cmd = g_strdup_printf ("gvfs-open %s", escaped_uri);
        } else if (g_find_program_in_path ("xdg-open") != NULL) {
                cmd = g_strdup_printf ("xdg-open %s", escaped_uri);
        } else if (g_find_program_in_path ("firefox") != NULL) {
                cmd = g_strdup_printf ("firefox %s", escaped_uri);
        } else {
                g_warning ("Unable to find a browser.");
        }

        g_free (escaped_uri);

        if (cmd != NULL) {
                g_spawn_command_line_async (cmd, NULL);
                g_free (cmd);
        }

        return TRUE;
}

static void
on_close_button_clicked (GtkButton *button,
                         NdBubble  *bubble)
{
        nd_notification_close (bubble->priv->notification, ND_NOTIFICATION_CLOSED_USER);
        gtk_widget_destroy (GTK_WIDGET (bubble));
}

static void
on_style_set (GtkWidget  *widget,
              GtkStyle   *previous_style,
              NdBubble  *bubble)
{
        g_signal_handlers_block_by_func (G_OBJECT (widget), on_style_set, bubble);
        override_style (widget, previous_style);

        gtk_widget_queue_draw (widget);

        g_signal_handlers_unblock_by_func (G_OBJECT (widget), on_style_set, bubble);
}

static void
nd_bubble_init (NdBubble *bubble)
{
        GtkWidget   *main_vbox;
        GtkWidget   *vbox;
        GtkWidget   *close_button;
        GtkWidget   *image;
        GtkWidget   *alignment;
        AtkObject   *atkobj;
        GtkRcStyle  *rcstyle;
        GdkScreen   *screen;
        GdkVisual   *visual;

        bubble->priv = ND_BUBBLE_GET_PRIVATE (bubble);

        g_signal_connect (G_OBJECT (bubble),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);

        gtk_widget_add_events (GTK_WIDGET (bubble), GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        atk_object_set_role (gtk_widget_get_accessible (GTK_WIDGET (bubble)), ATK_ROLE_ALERT);

        screen = gtk_window_get_screen (GTK_WINDOW (bubble));
        visual = gdk_screen_get_rgba_visual (screen);
        if (visual == NULL) {
                visual = gdk_screen_get_system_visual (screen);
         }

        gtk_widget_set_visual (GTK_WIDGET (bubble), visual);

        if (gdk_screen_is_composited (screen)) {
                bubble->priv->composited = TRUE;
        }

        main_vbox = gtk_vbox_new (FALSE, 0);
        g_signal_connect (G_OBJECT (main_vbox),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);
        gtk_widget_show (main_vbox);
        gtk_container_add (GTK_CONTAINER (bubble), main_vbox);
        gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);

        bubble->priv->main_hbox = gtk_hbox_new (FALSE, 0);
        gtk_widget_show (bubble->priv->main_hbox);
        gtk_box_pack_start (GTK_BOX (main_vbox),
                            bubble->priv->main_hbox,
                            FALSE, FALSE, 0);

        /* First row (icon, vbox, close) */
        bubble->priv->iconbox = gtk_alignment_new (0.5, 0, 0, 0);
        gtk_widget_show (bubble->priv->iconbox);
        gtk_alignment_set_padding (GTK_ALIGNMENT (bubble->priv->iconbox),
                                   5, 0, 0, 0);
        gtk_box_pack_start (GTK_BOX (bubble->priv->main_hbox),
                            bubble->priv->iconbox,
                            FALSE, FALSE, 0);
        gtk_widget_set_size_request (bubble->priv->iconbox, BODY_X_OFFSET, -1);

        bubble->priv->icon = gtk_image_new ();
        gtk_widget_show (bubble->priv->icon);
        gtk_container_add (GTK_CONTAINER (bubble->priv->iconbox), bubble->priv->icon);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_widget_show (vbox);
        gtk_box_pack_start (GTK_BOX (bubble->priv->main_hbox), vbox, TRUE, TRUE, 0);
        gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

        /* Add the close button */
        alignment = gtk_alignment_new (0.5, 0, 0, 0);
        gtk_widget_show (alignment);
        gtk_box_pack_start (GTK_BOX (bubble->priv->main_hbox), alignment, FALSE, FALSE, 0);

        close_button = gtk_button_new ();
        g_signal_connect (G_OBJECT (close_button),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);
        gtk_widget_show (close_button);
        bubble->priv->close_button = close_button;
        gtk_container_add (GTK_CONTAINER (alignment), close_button);
        gtk_button_set_relief (GTK_BUTTON (close_button), GTK_RELIEF_NONE);
        gtk_container_set_border_width (GTK_CONTAINER (close_button), 0);
        g_signal_connect (G_OBJECT (close_button),
                          "clicked",
                          G_CALLBACK (on_close_button_clicked),
                          bubble);

        rcstyle = gtk_rc_style_new ();
        rcstyle->xthickness = rcstyle->ythickness = 0;
        gtk_widget_modify_style (close_button, rcstyle);
        g_object_unref (rcstyle);

        atkobj = gtk_widget_get_accessible (close_button);
        atk_action_set_description (ATK_ACTION (atkobj), 0,
                                    "Closes the notification.");
        atk_object_set_name (atkobj, "");
        atk_object_set_description (atkobj, "Closes the notification.");

        image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (close_button), image);

        /* center vbox */
        bubble->priv->summary_label = gtk_label_new (NULL);
        g_signal_connect (G_OBJECT (bubble->priv->summary_label),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);
        gtk_widget_show (bubble->priv->summary_label);
        gtk_box_pack_start (GTK_BOX (vbox), bubble->priv->summary_label, TRUE, TRUE, 0);
        gtk_misc_set_alignment (GTK_MISC (bubble->priv->summary_label), 0, 0);
        gtk_label_set_line_wrap (GTK_LABEL (bubble->priv->summary_label), TRUE);

        atkobj = gtk_widget_get_accessible (bubble->priv->summary_label);
        atk_object_set_description (atkobj, "Notification summary text.");

        bubble->priv->content_hbox = gtk_hbox_new (FALSE, 6);
        gtk_widget_show (bubble->priv->content_hbox);
        gtk_box_pack_start (GTK_BOX (vbox), bubble->priv->content_hbox, FALSE, FALSE, 0);


        vbox = gtk_vbox_new (FALSE, 6);
        gtk_widget_show (vbox);
        gtk_box_pack_start (GTK_BOX (bubble->priv->content_hbox), vbox, TRUE, TRUE, 0);

        bubble->priv->body_label = gtk_label_new (NULL);
        g_signal_connect (G_OBJECT (bubble->priv->body_label),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);
        gtk_widget_show (bubble->priv->body_label);
        gtk_box_pack_start (GTK_BOX (vbox), bubble->priv->body_label, TRUE, TRUE, 0);
        gtk_misc_set_alignment (GTK_MISC (bubble->priv->body_label), 0, 0);
        gtk_label_set_line_wrap (GTK_LABEL (bubble->priv->body_label), TRUE);
        g_signal_connect (bubble->priv->body_label,
                          "activate-link",
                          G_CALLBACK (on_activate_link),
                          bubble);

        atkobj = gtk_widget_get_accessible (bubble->priv->body_label);
        atk_object_set_description (atkobj, "Notification body text.");

        alignment = gtk_alignment_new (1, 0.5, 0, 0);
        gtk_widget_show (alignment);
        gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, TRUE, 0);

        bubble->priv->actions_box = gtk_hbox_new (FALSE, 6);
        gtk_widget_show (bubble->priv->actions_box);
        gtk_container_add (GTK_CONTAINER (alignment), bubble->priv->actions_box);
}

static void
nd_bubble_finalize (GObject *object)
{
        NdBubble *bubble;

        g_return_if_fail (object != NULL);
        g_return_if_fail (ND_IS_BUBBLE (object));

        bubble = ND_BUBBLE (object);

        g_return_if_fail (bubble->priv != NULL);

        if (bubble->priv->timeout_id != 0) {
                g_source_remove (bubble->priv->timeout_id);
        }

        g_signal_handlers_disconnect_by_func (bubble->priv->notification, G_CALLBACK (on_notification_changed), bubble);

        g_object_unref (bubble->priv->notification);

        G_OBJECT_CLASS (nd_bubble_parent_class)->finalize (object);
}

static void
update_content_hbox_visibility (NdBubble *bubble)
{
        if (bubble->priv->have_icon
            || bubble->priv->have_body
            || bubble->priv->have_actions) {
                gtk_widget_show (bubble->priv->content_hbox);
        } else {
                gtk_widget_hide (bubble->priv->content_hbox);
        }
}

static void
set_notification_text (NdBubble   *bubble,
                       const char *summary,
                       const char *body)
{
        char          *str;
        char          *quoted;
        GtkRequisition req;
        int            summary_width;

        quoted = g_markup_escape_text (summary, -1);
        str = g_strdup_printf ("<b><big>%s</big></b>", quoted);
        g_free (quoted);

        gtk_label_set_markup (GTK_LABEL (bubble->priv->summary_label), str);

        g_free (str);
        gtk_widget_show_all (GTK_WIDGET (bubble));
        gtk_label_set_markup (GTK_LABEL (bubble->priv->body_label), body);

        if (body == NULL || *body == '\0') {
                bubble->priv->have_body = FALSE;
                gtk_widget_hide (bubble->priv->body_label);
        } else {
                bubble->priv->have_body = TRUE;
                gtk_widget_show (bubble->priv->body_label);
        }
        update_content_hbox_visibility (bubble);

        gtk_widget_size_request (bubble->priv->close_button, &req);
        /* -1: main_vbox border width
           -10: vbox border width
           -6: spacing for hbox */
        summary_width = WIDTH - (1*2) - (10*2) - BODY_X_OFFSET - req.width - (6*2);

        if (body != NULL && *body != '\0') {
                gtk_widget_set_size_request (bubble->priv->body_label,
                                             summary_width,
                                             -1);
        }

        gtk_widget_set_size_request (bubble->priv->summary_label,
                                     summary_width,
                                     -1);
}

static GdkPixbuf *
scale_pixbuf (GdkPixbuf *pixbuf,
              int        max_width,
              int        max_height,
              gboolean   no_stretch_hint)
{
        int        pw;
        int        ph;
        float      scale_factor_x = 1.0;
        float      scale_factor_y = 1.0;
        float      scale_factor = 1.0;

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        /* Determine which dimension requires the smallest scale. */
        scale_factor_x = (float) max_width / (float) pw;
        scale_factor_y = (float) max_height / (float) ph;

        if (scale_factor_x > scale_factor_y) {
                scale_factor = scale_factor_y;
        } else {
                scale_factor = scale_factor_x;
        }

        /* always scale down, allow to disable scaling up */
        if (scale_factor < 1.0 || !no_stretch_hint) {
                int scale_x;
                int scale_y;

                scale_x = (int) (pw * scale_factor);
                scale_y = (int) (ph * scale_factor);
                return gdk_pixbuf_scale_simple (pixbuf,
                                                scale_x,
                                                scale_y,
                                                GDK_INTERP_BILINEAR);
        } else {
                return g_object_ref (pixbuf);
        }
}

static void
set_notification_icon (NdBubble  *bubble,
                       GdkPixbuf *pixbuf)
{
        GdkPixbuf  *scaled;

        scaled = NULL;
        if (pixbuf != NULL) {
                scaled = scale_pixbuf (pixbuf,
                                       MAX_ICON_SIZE,
                                       MAX_ICON_SIZE,
                                       TRUE);
        }

        gtk_image_set_from_pixbuf (GTK_IMAGE (bubble->priv->icon), scaled);

        if (scaled != NULL) {
                int pixbuf_width = gdk_pixbuf_get_width (scaled);

                gtk_widget_show (bubble->priv->icon);
                gtk_widget_set_size_request (bubble->priv->iconbox,
                                             MAX (BODY_X_OFFSET, pixbuf_width), -1);
                g_object_unref (scaled);
                bubble->priv->have_icon = TRUE;
        } else {
                gtk_widget_hide (bubble->priv->icon);
                gtk_widget_set_size_request (bubble->priv->iconbox,
                                             BODY_X_OFFSET,
                                             -1);
                bubble->priv->have_icon = FALSE;
        }

        update_content_hbox_visibility (bubble);
}

static void
on_action_clicked (GtkButton      *button,
                   GdkEventButton *event,
                   NdBubble       *bubble)
{
        const char *key = g_object_get_data (G_OBJECT (button), "_action_key");

        nd_notification_action_invoked (bubble->priv->notification,
                                        key);
        gtk_widget_destroy (GTK_WIDGET (bubble));
}

static void
add_notification_action (NdBubble       *bubble,
                         const char     *text,
                         const char     *key)
{
        GtkWidget *button;
        GtkWidget *hbox;
        GdkPixbuf *pixbuf;
        char      *buf;

        if (!gtk_widget_get_visible (bubble->priv->actions_box)) {
                GtkWidget *alignment;

                gtk_widget_show (bubble->priv->actions_box);
                update_content_hbox_visibility (bubble);

                alignment = gtk_alignment_new (1, 0.5, 0, 0);
                gtk_widget_show (alignment);
                gtk_box_pack_end (GTK_BOX (bubble->priv->actions_box),
                                  alignment,
                                  FALSE, TRUE, 0);
        }

        button = gtk_button_new ();
        g_signal_connect (G_OBJECT (button),
                          "style-set",
                          G_CALLBACK (on_style_set),
                          bubble);
        gtk_widget_show (button);
        gtk_box_pack_start (GTK_BOX (bubble->priv->actions_box), button, FALSE, FALSE, 0);
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        gtk_container_set_border_width (GTK_CONTAINER (button), 0);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_widget_show (hbox);
        gtk_container_add (GTK_CONTAINER (button), hbox);

        pixbuf = NULL;
        /* try to load an icon if requested */
        if (nd_notification_get_action_icons (bubble->priv->notification)) {
                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (bubble))),
                                                   key,
                                                   20,
                                                   GTK_ICON_LOOKUP_USE_BUILTIN,
                                                   NULL);
        }

        if (pixbuf != NULL) {
                GtkWidget *image;

                image = gtk_image_new_from_pixbuf (pixbuf);
                g_object_unref (pixbuf);
                g_signal_connect (G_OBJECT (image),
                                  "style-set",
                                  G_CALLBACK (on_style_set),
                                  bubble);
                atk_object_set_name (gtk_widget_get_accessible (GTK_WIDGET (button)),
                                     text);
                gtk_widget_show (image);
                gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
                gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.5);
        } else {
                GtkWidget *label;

                label = gtk_label_new (NULL);
                g_signal_connect (G_OBJECT (label),
                                  "style-set",
                                  G_CALLBACK (on_style_set),
                                  bubble);
                gtk_widget_show (label);
                gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
                gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
                buf = g_strdup_printf ("<small>%s</small>", text);
                gtk_label_set_markup (GTK_LABEL (label), buf);
                g_free (buf);
        }

        g_object_set_data_full (G_OBJECT (button),
                                "_action_key", g_strdup (key), g_free);
        g_signal_connect (G_OBJECT (button),
                          "button-release-event",
                          G_CALLBACK (on_action_clicked),
                          bubble);
}

static void
clear_actions (NdBubble *bubble)
{
        gtk_widget_hide (bubble->priv->actions_box);
        gtk_container_foreach (GTK_CONTAINER (bubble->priv->actions_box),
                               (GtkCallback)gtk_widget_destroy,
                               NULL);
        bubble->priv->have_actions = FALSE;
}

static void
add_actions (NdBubble *bubble)
{
        char **actions;
        int    i;

        actions = nd_notification_get_actions (bubble->priv->notification);

        for (i = 0; actions[i] != NULL; i += 2) {
                char *l = actions[i + 1];

                if (l == NULL) {
                        g_warning ("Label not found for action %s. "
                                   "The protocol specifies that a label must "
                                   "follow an action in the actions array",
                                   actions[i]);

                        break;
                }

                if (strcasecmp (actions[i], "default") != 0) {
                        add_notification_action (bubble,
                                                 l,
                                                 actions[i]);
                        bubble->priv->have_actions = TRUE;
                }
        }
}

static void
update_image (NdBubble *bubble)
{
        GdkPixbuf *pixbuf;

        pixbuf = nd_notification_load_image (bubble->priv->notification, IMAGE_SIZE);
        if (pixbuf != NULL) {
                set_notification_icon (bubble, pixbuf);
                g_object_unref (G_OBJECT (pixbuf));
        }
}

static void
update_bubble (NdBubble *bubble)
{
        set_notification_text (bubble,
                               nd_notification_get_summary (bubble->priv->notification),
                               nd_notification_get_body (bubble->priv->notification));
        clear_actions (bubble);
        add_actions (bubble);
        update_image (bubble);
        update_content_hbox_visibility (bubble);
}

static void
on_notification_changed (NdNotification *notification,
                         NdBubble       *bubble)
{
        update_bubble (bubble);
}

NdBubble *
nd_bubble_new_for_notification (NdNotification *notification)
{
        NdBubble *bubble;

        bubble = g_object_new (ND_TYPE_BUBBLE,
                               "app-paintable", TRUE,
                               "type", GTK_WINDOW_POPUP,
                               "title", "Notification",
                               "resizable", FALSE,
                               "type-hint", GDK_WINDOW_TYPE_HINT_NOTIFICATION,
                               NULL);
        bubble->priv->notification = g_object_ref (notification);
        g_signal_connect (notification, "changed", G_CALLBACK (on_notification_changed), bubble);
        update_bubble (bubble);

        return bubble;
}
