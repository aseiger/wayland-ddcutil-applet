/*
 * bar_applet - wf-panel-pi plugin for Raspberry Pi OS
 *
 * Provides brightness and volume controls in the Wayland taskbar.
 *
 * Copyright (c) 2026 - MIT License
 */

#include <gtk/gtk.h>

#include <gtkmm/hvbox.h>

#include <widget.hpp>

extern "C" {
#include <lxutils.h>
}

extern "C" {
#include "brightness.h"
#include "volume.h"
#include "lcdstats.h"
}

/* ------------------------------------------------------------------ */
/*  Plugin class                                                      */
/* ------------------------------------------------------------------ */

class BarAppletWidget : public WayfireWidget
{
    GtkWidget     *panel_button = nullptr;   /* raw C GtkButton      */
    GtkWidget     *panel_icon   = nullptr;   /* raw C GtkImage       */
    GtkWidget     *popup_window = nullptr;
    GtkWidget     *brightness_scale = nullptr;
    GtkWidget     *volume_scale = nullptr;

    /* -------------------------------------------------------------- */
    /*  Build the popup window (plain GtkWindow, not a popover)       */
    /* -------------------------------------------------------------- */

    void build_popup()
    {
        /* Destroy any previous popup — the framework's close_popup()
         * invalidates the layer-shell state, so we must rebuild. */
        if (popup_window)
        {
            gtk_widget_destroy(popup_window);
            popup_window = nullptr;
        }

        popup_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(popup_window), FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(popup_window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(popup_window), TRUE);
        gtk_container_set_border_width(GTK_CONTAINER(popup_window), 8);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_add(GTK_CONTAINER(popup_window), vbox);

        /* --- Brightness ------------------------------------------- */
        GtkWidget *br_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(br_label), "<b>Brightness</b>");
        gtk_widget_set_halign(br_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox), br_label, FALSE, FALSE, 4);

        int br_max = brightness_get_max();
        brightness_scale = gtk_scale_new_with_range(
            GTK_ORIENTATION_HORIZONTAL, 0, br_max > 0 ? br_max : 100, 1);
        gtk_scale_set_draw_value(GTK_SCALE(brightness_scale), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(brightness_scale), GTK_POS_RIGHT);
        gtk_widget_set_size_request(brightness_scale, 220, -1);
        g_signal_connect(brightness_scale, "button-release-event",
                         G_CALLBACK(on_brightness_released_cb), this);
        gtk_box_pack_start(GTK_BOX(vbox), brightness_scale, FALSE, FALSE, 0);

        /* --- Separator -------------------------------------------- */
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 6);

        /* --- Volume ----------------------------------------------- */
        GtkWidget *vol_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(vol_label), "<b>Volume</b>");
        gtk_widget_set_halign(vol_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox), vol_label, FALSE, FALSE, 4);

        volume_scale = gtk_scale_new_with_range(
            GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_scale_set_draw_value(GTK_SCALE(volume_scale), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(volume_scale), GTK_POS_RIGHT);
        gtk_widget_set_size_request(volume_scale, 220, -1);
        g_signal_connect(volume_scale, "button-release-event",
                         G_CALLBACK(on_volume_released_cb), this);
        gtk_box_pack_start(GTK_BOX(vbox), volume_scale, FALSE, FALSE, 0);

        gtk_widget_show_all(popup_window);
        gtk_widget_hide(popup_window);
    }

    /* -------------------------------------------------------------- */
    /*  Callbacks (static C-style for g_signal_connect)               */
    /* -------------------------------------------------------------- */

    static gboolean on_brightness_released_cb(GtkWidget *, GdkEventButton *,
                                               gpointer data)
    {
        auto *self = static_cast<BarAppletWidget *>(data);
        int val = (int)gtk_range_get_value(GTK_RANGE(self->brightness_scale));
        brightness_set(val);
        lcdstats_send_brightness(val);
        return FALSE;
    }

    static gboolean on_volume_released_cb(GtkWidget *, GdkEventButton *,
                                           gpointer data)
    {
        auto *self = static_cast<BarAppletWidget *>(data);
        int val = (int)gtk_range_get_value(GTK_RANGE(self->volume_scale));
        volume_set(val);
        lcdstats_send_volume(val);
        return FALSE;
    }

    /* Click handler for the panel button */
    static void on_button_clicked(GtkButton *, gpointer data)
    {
        auto *self = static_cast<BarAppletWidget *>(data);
        self->show_popup();
    }

    /* Show the popup at the panel button (framework handles dismiss) */
    void show_popup()
    {
        build_popup();

        /* Refresh current values from the display */
        int br = brightness_get();
        if (br >= 0)
            gtk_range_set_value(GTK_RANGE(brightness_scale), br);

        int vol = volume_get();
        if (vol >= 0)
            gtk_range_set_value(GTK_RANGE(volume_scale), vol);

        /* Use the wf-panel-pi framework popup helper — this handles
         * positioning, layer-shell, and auto-dismiss on click-outside. */
        popup_window_at_button(popup_window, panel_button);
    }

  public:
    void init(Gtk::HBox *container) override
    {
        /* Use raw C GTK widgets — this matches the stock plugin pattern
         * and ensures find_panel() can locate the panel window correctly
         * for popup positioning. */
        panel_icon = gtk_image_new_from_icon_name(
            "preferences-desktop-display", GTK_ICON_SIZE_BUTTON);

        panel_button = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(panel_button), GTK_RELIEF_NONE);
        gtk_container_add(GTK_CONTAINER(panel_button), panel_icon);
        gtk_widget_set_tooltip_text(panel_button,
                                    "Display Settings – Brightness & Volume");

        g_signal_connect(panel_button, "clicked",
                         G_CALLBACK(on_button_clicked), this);

        /* Pack the raw C button into the gtkmm HBox via its C handle */
        gtk_box_pack_start(GTK_BOX(container->gobj()),
                           panel_button, FALSE, FALSE, 0);
        gtk_widget_show_all(panel_button);

        /* Connect to lcdstats daemon — current brightness & volume
         * are sent automatically on connect (and on any reconnect). */
        lcdstats_init();
    }

    ~BarAppletWidget() override
    {
        lcdstats_close();
        if (popup_window)
            gtk_widget_destroy(popup_window);
    }
};

/* ------------------------------------------------------------------ */
/*  Plugin entry points (extern "C")                                  */
/* ------------------------------------------------------------------ */

extern "C" {

WayfireWidget *create()
{
    return new BarAppletWidget();
}

void destroy(WayfireWidget *w)
{
    delete w;
}

const char *display_name()
{
    return "Display Settings";
}

const char *package_name()
{
    return "bar_applet";
}

const conf_table_t *config_params()
{
    /* No user-configurable params yet */
    static const conf_table_t params[] = {
        {CONF_NONE, NULL, NULL}
    };
    return params;
}

} /* extern "C" */
