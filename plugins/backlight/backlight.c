/*
 * 
 * 
 */

#include "misc.h"
#include "../meter/meter.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "dbg.h"

#define BRIGHTNESS_MAX  2.0
#define BRIGHTNESS_MIN  0.2
#define BRIGHTNESS_STEP 0.1

static gchar *names[] = {
    "brightness_icon",
    NULL
};

typedef struct {
    meter_priv meter;
    Display * display;
    gint screen;
    XF86VidModeGamma gamma;
    gint update_id, leave_id;
    gint has_pointer;
    GtkWidget *slider_window;
    GtkWidget *slider;
} backlight_priv;

static meter_class *k;

static void slider_changed(GtkRange *range, backlight_priv *c);

static gfloat
perc2f(gint value, gfloat max)
{
    return (((gfloat) value) * max / 100);
}

static gint
f2perc(gfloat value, gfloat max)
{
    return (gint) (value * 100 / max)+((fmodf(value*100, max) > 0.5) ? 1 : 0);
}

static gboolean
get_gamma(backlight_priv *c)
{
    ENTER;
    if (!c->display)
        RET(0);
    if (!XF86VidModeGetGamma(c->display, c->screen, &c->gamma)) {
        DBG("backlight: can't get gamma.\n");
        RET(0);
    }
    DBG("backlight: gamma.red=%f, gamma.green=%f, gamma.blue=%f\n", c->gamma.red, c->gamma.green, c->gamma.blue);
    RET(1);
}

static void
set_gamma(backlight_priv *c, gfloat brightness)
{
    ENTER;
    if (!c->display)
        return;
    c->gamma.red = brightness;
    c->gamma.green = c->gamma.red;
    c->gamma.blue = c->gamma.red;
    
    if (!XF86VidModeSetGamma(c->display, c->screen, &c->gamma)) {
        DBG("backlight: can't set gamma.\n");
        return;
    }
    DBG("backlight: gamma.red=%f, gamma.green=%f, gamma.blue=%f\n", c->gamma.red, c->gamma.green, c->gamma.blue);
}

static gboolean
brightness_update_gui(backlight_priv *c)
{
    gfloat brightness;
    gchar buf[30];
    ENTER;
    get_gamma(c);
    brightness = c->gamma.red;
    k->set_level(&c->meter, f2perc(brightness, BRIGHTNESS_MAX));
    g_snprintf(buf, sizeof(buf), "<b>Brightness:</b> %-.2f%%", brightness);
    if (!c->slider_window)
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, buf);
    else {
        g_signal_handlers_block_by_func(G_OBJECT(c->slider),
            G_CALLBACK(slider_changed), c);
        gtk_range_set_value(GTK_RANGE(c->slider), brightness);
        g_signal_handlers_unblock_by_func(G_OBJECT(c->slider),
            G_CALLBACK(slider_changed), c);
    }
    RET(TRUE);
}

static void
slider_changed(GtkRange *range, backlight_priv *c)
{
    gfloat brightness = gtk_range_get_value(range);
    ENTER;
    set_gamma(c, brightness);
    brightness_update_gui(c);
    RET();
}

static GtkWidget *
brightness_create_slider(backlight_priv *c)
{
    GtkWidget *slider, *win;
    GtkWidget *frame;
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 180, 180);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 1);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_MOUSE);
    gtk_window_stick(GTK_WINDOW(win));
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(win), frame);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 1);
    slider = gtk_vscale_new_with_range(BRIGHTNESS_MIN, BRIGHTNESS_MAX, BRIGHTNESS_STEP);
    gtk_widget_set_size_request(slider, 25, 82);
    gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_BOTTOM);
    gtk_scale_set_digits(GTK_SCALE(slider), 2);
    gtk_range_set_inverted(GTK_RANGE(slider), TRUE);
    gtk_range_set_value(GTK_RANGE(slider), c->gamma.red);
    g_signal_connect(G_OBJECT(slider), "value_changed", G_CALLBACK(slider_changed), c);
    gtk_container_add(GTK_CONTAINER(frame), slider);
    c->slider = slider;
    return win;
}  

static gboolean
icon_clicked(GtkWidget *widget, GdkEventButton *event, backlight_priv *c)
{
    ENTER;
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        if (c->slider_window == NULL) {
            c->slider_window = brightness_create_slider(c);
            gtk_widget_show_all(c->slider_window);
            gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, NULL);
        } else {
            gtk_widget_destroy(c->slider_window);
            c->slider_window = NULL;
            if (c->leave_id) {
                g_source_remove(c->leave_id);
                c->leave_id = 0;
            }
        }
        RET(FALSE);
    }
    
    brightness_update_gui(c);
    RET(FALSE);
}

static gboolean
icon_scrolled(GtkWidget *widget, GdkEventScroll *event, backlight_priv *c)
{
    gfloat brightness;
    
    ENTER;
    brightness = perc2f(((meter_priv *) c)->level, BRIGHTNESS_MAX);
    brightness += ((event->direction == GDK_SCROLL_UP
            || event->direction == GDK_SCROLL_LEFT) ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP);
    if (brightness > BRIGHTNESS_MAX)
        brightness = BRIGHTNESS_MAX;
    if (brightness <= BRIGHTNESS_MIN)
        brightness = BRIGHTNESS_MIN;
    set_gamma(c, brightness);
    brightness_update_gui(c);
    RET(TRUE);
}

static gint
backlight_constructor(plugin_instance *p)
{
    gint major, minor;
    backlight_priv *c;
    ENTER;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (backlight_priv *) p;
    c->display = XOpenDisplay(NULL);
    if (!c->display)
        RET(0);
    if (!XF86VidModeQueryVersion(c->display, &major, &minor) || major < 2 || (major == 2 && minor < 0))
        RET(0);
    c->screen = DefaultScreen(c->display);
    get_gamma(c);
    k->set_icons(&c->meter, names);
    c->update_id = g_timeout_add(1000, (GSourceFunc) brightness_update_gui, c);
    brightness_update_gui(c);
    g_signal_connect(G_OBJECT(p->pwid), "scroll-event",
        G_CALLBACK(icon_scrolled), (gpointer) c);
    g_signal_connect(G_OBJECT(p->pwid), "button_press_event", G_CALLBACK(icon_clicked), (gpointer)c);
    RET(1);
}

static void
backlight_destructor(plugin_instance *p)
{
    backlight_priv *c = (backlight_priv *) p;
    ENTER;
    XCloseDisplay(c->display);
    g_source_remove(c->update_id);
    if (c->slider_window)
        gtk_widget_destroy(c->slider_window);
    PLUGIN_CLASS(k)->destructor(p);
    class_put("meter");
    RET();
}

static plugin_class class = {
    .count       = 0,
    .type        = "backlight",
    .name        = "Backlight",
    .version     = "2.0",
    .description = "Backlight control",
    .priv_size   = sizeof(backlight_priv),
    .constructor = backlight_constructor,
    .destructor  = backlight_destructor,
};

static plugin_class *class_ptr = (plugin_class *) &class;
