/*
 * 
 * 
 */

#include "misc.h"
#include "../meter/meter.h"
//#include "run.h"
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
    XF86VidModeGamma gamma;
    int update_id, leave_id;
    int has_pointer;
    GtkWidget *slider_window;
    GtkWidget *slider;
} backlight_priv;

static meter_class *k;

static void slider_changed(GtkRange *range, backlight_priv *c);

static gboolean
get_gamma(backlight_priv *c)
{
    Display *display;
    int screen;
    int major, minor;
    ENTER;
    display = XOpenDisplay(NULL);
    if (!display) RET(0);
    screen = DefaultScreen(display);
    if (!XF86VidModeQueryVersion(display, &major, &minor)
            || major < 2 || (major == 2 && minor < 0)
            || !XF86VidModeGetGamma(display, screen, &c->gamma)) {
        XCloseDisplay(display);
        DBG("backlight: can't get gamma.\n");
        RET(0);
    }
    DBG("backlight: gamma.red=%f, gamma.green=%f, gamma.blue=%f\n", c->gamma.red, c->gamma.green, c->gamma.blue);
    RET(1);
}

static void
set_gamma(backlight_priv *c, gfloat brightness)
{
    XF86VidModeGamma gamma;
    Display *display;
    int screen, major, minor;
    ENTER;
    DBG("backlight: brightness=%f\n", brightness);
    display = XOpenDisplay(NULL);
    if (!display)
        return;
    if (!XF86VidModeQueryVersion(display, &major, &minor)
            || major < 2 || (major == 2 && minor < 0)) {
        XCloseDisplay(display);
        return;
    }
    gamma.red = brightness;
    gamma.green = gamma.red;
    gamma.blue = gamma.red;
    
    screen = DefaultScreen(display);
    if (!XF86VidModeSetGamma(display, screen, &gamma)) {
        XF86VidModeSetGamma(display, screen, &c->gamma);
        XF86VidModeGetGamma(display, screen, &c->gamma);
        XCloseDisplay(display);
        return;
    }
    if (!XF86VidModeGetGamma(display, screen, &gamma)) {
        XF86VidModeSetGamma(display, screen, &c->gamma);
        XF86VidModeGetGamma(display, screen, &c->gamma);
        XCloseDisplay(display);
        return;
    }
    DBG("gamma -> %f %f %f\n", gamma.red, gamma.green, gamma.blue);
    c->gamma.red = gamma.red;
    c->gamma.green = gamma.green;
    c->gamma.blue = gamma.blue;
    XCloseDisplay(display);
}

static gboolean
brightness_update_gui(backlight_priv *c)
{
    gfloat brightness;
    gchar buf[30];
    ENTER;
    brightness = c->gamma.red;
    k->set_level(&c->meter, (int) (brightness * 100 / BRIGHTNESS_MAX));
    DBG("meter.level:%f\n", ((meter_priv *) c)->level);
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
    DBG("value=%f\n", brightness);
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
    gtk_range_set_value(GTK_RANGE(slider), ((meter_priv *) c)->level);
    DBG("meter->level %f\n", ((meter_priv *) c)->level);
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
    brightness = ((gfloat)((meter_priv *) c)->level) * BRIGHTNESS_MAX/100;
    DBG("icon_scrolled: c->level=%f\n", ((meter_priv*)c)->level);
    brightness += ((event->direction == GDK_SCROLL_UP
            || event->direction == GDK_SCROLL_LEFT) ? 0.1 : -0.1);
    
    if (brightness > BRIGHTNESS_MAX)
        brightness = BRIGHTNESS_MAX;
    if (brightness <= BRIGHTNESS_MIN)
        brightness = BRIGHTNESS_MIN;
    
    set_gamma(c, brightness);
    brightness_update_gui(c);
    RET(TRUE);
}

static int
backlight_constructor(plugin_instance *p)
{
    backlight_priv *c;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (backlight_priv *) p;
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
