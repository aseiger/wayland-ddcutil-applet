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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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

    int            last_brightness = -1;
    int            last_volume = -1;

    std::thread             poll_thread;
    std::atomic<bool>       poll_stop{false};
    std::mutex              poll_mutex;       /* guards poll_cv wait     */
    std::condition_variable poll_cv;
    std::mutex              io_mutex;         /* serializes DDC/CI access */

    static constexpr int POLL_INTERVAL_MS = 1000;

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
        {
            std::lock_guard<std::mutex> lk(self->io_mutex);
            brightness_set(val);
        }
        lcdstats_send_brightness(val);
        self->last_brightness = val;
        return FALSE;
    }

    static gboolean on_volume_released_cb(GtkWidget *, GdkEventButton *,
                                           gpointer data)
    {
        auto *self = static_cast<BarAppletWidget *>(data);
        int val = (int)gtk_range_get_value(GTK_RANGE(self->volume_scale));
        {
            std::lock_guard<std::mutex> lk(self->io_mutex);
            volume_set(val);
        }
        lcdstats_send_volume(val);
        self->last_volume = val;
        return FALSE;
    }

    /* Result of one polling cycle, applied on the GTK main thread */
    struct PollResult
    {
        BarAppletWidget *self;
        int              brightness;   /* -1 == unchanged */
        int              volume;       /* -1 == unchanged */
    };

    /* Apply mirrored changes on the main loop (UI + lcdstats) */
    static gboolean apply_poll_result_cb(gpointer data)
    {
        auto *r = static_cast<PollResult *>(data);
        auto *self = r->self;

        if (r->brightness >= 0)
        {
            self->last_brightness = r->brightness;
            lcdstats_send_brightness(r->brightness);
            if (self->popup_window && gtk_widget_get_visible(self->popup_window) && self->brightness_scale)
                gtk_range_set_value(GTK_RANGE(self->brightness_scale), r->brightness);
        }

        if (r->volume >= 0)
        {
            self->last_volume = r->volume;
            lcdstats_send_volume(r->volume);
            if (self->popup_window && gtk_widget_get_visible(self->popup_window) && self->volume_scale)
                gtk_range_set_value(GTK_RANGE(self->volume_scale), r->volume);
        }

        delete r;
        return G_SOURCE_REMOVE;
    }

    /* Background poll loop: slow DDC/CI reads off the main thread */
    void poll_loop()
    {
        while (!poll_stop)
        {
            int br, vol;
            {
                std::lock_guard<std::mutex> lk(io_mutex);
                br  = brightness_get();
                vol = volume_get();
            }

            int br_changed  = (br  >= 0 && br  != last_brightness) ? br  : -1;
            int vol_changed = (vol >= 0 && vol != last_volume)     ? vol : -1;
            if (br_changed >= 0 || vol_changed >= 0)
            {
                auto *r = new PollResult{this, br_changed, vol_changed};
                g_idle_add(apply_poll_result_cb, r);
            }

            std::unique_lock<std::mutex> lk(poll_mutex);
            poll_cv.wait_for(lk, std::chrono::milliseconds(POLL_INTERVAL_MS),
                             [this] { return poll_stop.load(); });
        }
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
        int br, vol;
        {
            std::lock_guard<std::mutex> lk(io_mutex);
            br  = brightness_get();
            vol = volume_get();
        }
        if (br >= 0)
        {
            gtk_range_set_value(GTK_RANGE(brightness_scale), br);
            last_brightness = br;
        }

        if (vol >= 0)
        {
            gtk_range_set_value(GTK_RANGE(volume_scale), vol);
            last_volume = vol;
        }

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

        last_brightness = brightness_get();
        last_volume = volume_get();

        /* Poll at ~1Hz on a worker thread so the slow DDC/CI reads never
         * block the GTK main loop. External changes are mirrored to
         * lcdstats and the popup sliders without hammering the I2C bus. */
        poll_thread = std::thread(&BarAppletWidget::poll_loop, this);
    }

    ~BarAppletWidget() override
    {
        poll_stop = true;
        poll_cv.notify_all();
        if (poll_thread.joinable())
            poll_thread.join();
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
