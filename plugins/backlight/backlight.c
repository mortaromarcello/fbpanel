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

#include "dbg.h"

#define BACKLIGHT_SYSFS_LOCATION    "/sys/class/backlight"
#define BRIGHTNESS_SWITCH_LOCATION  "/sys/module/video/parameters/brightness_switch_enabled"

static gchar *names[] = {
//  "display-brightness",
    "stock_volume-min",
    NULL
};

static gchar *s_names[] = {
    "stock_volume-mute",
    NULL
};
  
typedef struct {
    meter_priv meter;
    gchar *backlight_brightness;
    gchar *backlight_max_brightness;
    gint brightness, max_brightness, muted_bright;
    int update_id, leave_id;
    int has_pointer;
    gboolean muted;
    GtkWidget *slider_window;
    GtkWidget *slider;
} backlight_priv;

static meter_class *k;

static void slider_changed(GtkRange *range, backlight_priv *c);
static gboolean crossed(GtkWidget *widget, GdkEventCrossing *event, backlight_priv *c);

/*
 * Find best backlight using an ordered interface list
 */
static gchar *
backlight_get_best_backlight (void)
{
    gchar *filename;
    guint i;
    gboolean ret;
    GDir *dir = NULL;
    GError *error = NULL;
    const gchar *first_device;

    /* available kernel interfaces in priority order */
    static const gchar *backlight_interfaces[] = {
        "nv_backlight",
        "asus_laptop",
        "toshiba",
        "eeepc",
        "thinkpad_screen",
        "intel_backlight",
        "acpi_video1",
        "mbp_backlight",
        "acpi_video0",
        "fujitsu-laptop",
        "sony",
        "samsung",
        NULL,
    };

    /* search each one */
    for (i=0; backlight_interfaces[i] != NULL; i++) {
        filename = g_build_filename (BACKLIGHT_SYSFS_LOCATION,
                         backlight_interfaces[i], NULL);
        ret = g_file_test (filename, G_FILE_TEST_EXISTS);
        if (ret)
            goto out;
        g_free (filename);
    }

    /* nothing found in the ordered list */
    filename = NULL;

    /* find any random ones */
    dir = g_dir_open (BACKLIGHT_SYSFS_LOCATION, 0, &error);
    if (dir == NULL) {
        g_warning ("failed to find any devices: %s", error->message);
        g_error_free (error);
        goto out;
    }

    /* get first device if any */
    first_device = g_dir_read_name (dir);
    if (first_device != NULL) {
        filename = g_build_filename (BACKLIGHT_SYSFS_LOCATION,
                         first_device, NULL);
    }
out:
    if (dir != NULL)
        g_dir_close (dir);
    return filename;
}

/*
static gboolean
backlight_write (const gchar *filename, gint value, GError **error)
{
    gchar *text = NULL;
    gint retval;
    gint length;
    gint fd = -1;
    gboolean ret = TRUE;

    fd = open (filename, O_WRONLY);
    if (fd < 0) {
        ret = FALSE;
        g_set_error (error, 1, 0, "failed to open filename: %s", filename);
        goto out;
    }

    // convert to text
    text = g_strdup_printf ("%i", value);
    length = strlen (text);

    // write to device file
    retval = write (fd, text, length);
    if (retval != length) {
        ret = FALSE;
        g_set_error (error, 1, 0, "writing '%s' to %s failed", text, filename);
        goto out;
    }
out:
    if (fd >= 0)
        close (fd);
    g_free (text);
    return ret;
}
*/

static gboolean
backlight_write(const gchar *filename, const gchar *value)
{
    return g_file_set_contents(filename, value, -1, NULL);
}

static gchar *
backlight_read(const gchar *filename)
{
    char *buf = NULL;
    gchar *value = NULL;
    if (g_file_get_contents(filename, &buf, NULL, NULL) == TRUE) {
        value = g_strdup( buf );
        value = g_strstrip( value );
        g_free( buf );
    }
    return value;
}

static gint
get_brightness(backlight_priv *c)
{
    gint brightness = -1;
    gchar *value = NULL;
    ENTER;
    if ((value = backlight_read(c->backlight_brightness)) == NULL) {
        DBG("backlight: can't get brightness.\n");
        RET(0);
    }
    brightness = g_ascii_strtoll(value, NULL, 0);
    DBG("backlight: brightness=%d\n", brightness);
    RET(brightness);
}

static gint
get_max_brightness(backlight_priv *c)
{
    gint max_brightness = -1;
    gchar *value = NULL;
    ENTER;
    if ((value = backlight_read(c->backlight_max_brightness)) == NULL) {
        DBG("backlight: can't get max_brightness.\n");
        RET(0);
    }
    max_brightness = g_ascii_strtoll(value, NULL, 0);
    DBG("backlight: max_brightness=%d\n", max_brightness);
    RET(max_brightness);
}

static void
set_brightness(backlight_priv *c, gint brightness)
{
    gchar *value = NULL;
    ENTER;
    DBG("backlight: brightness=%d\n", brightness);
    value = g_strdup_printf("%i", brightness);
    if (backlight_write(c->backlight_brightness, value) != TRUE) {
        DBG("backlight: don't set brightness.\n");
    }
}

static gboolean
brightness_update_gui(backlight_priv *c)
{
    gint brightness;
    gchar buf[20];

    ENTER;
    c->brightness = get_brightness(c);
    c->max_brightness = get_max_brightness(c);
    brightness = c->brightness / c->max_brightness * 100;
    if ((brightness != 0) != (c->brightness != 0)) {
        if (brightness)
            k->set_icons(&c->meter, names);
      else
            k->set_icons(&c->meter, s_names);
        DBG("setting %s icons\n", brightness ? "normal" : "muted");
    }
    //c->brightness = brightness;
    k->set_level(&c->meter, brightness);
    g_snprintf(buf, sizeof(buf), "<b>Brightness:</b> %d%%", brightness);
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
    int brightness = (int) gtk_range_get_value(range);
    ENTER;
    DBG("value=%d\n", brightness);
    c->brightness = brightness * c->max_brightness / 100;
    set_brightness(c, c->brightness);
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
    
    slider = gtk_vscale_new_with_range(0.0, 100.0, 1.0);
    gtk_widget_set_size_request(slider, 25, 82);
    gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_BOTTOM);
    gtk_scale_set_digits(GTK_SCALE(slider), 0);
    gtk_range_set_inverted(GTK_RANGE(slider), TRUE);
    gtk_range_set_value(GTK_RANGE(slider), ((meter_priv *) c)->level);
    DBG("meter->level %f\n", ((meter_priv *) c)->level);
    g_signal_connect(G_OBJECT(slider), "value_changed",
        G_CALLBACK(slider_changed), c);
    g_signal_connect(G_OBJECT(slider), "enter-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    g_signal_connect(G_OBJECT(slider), "leave-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    gtk_container_add(GTK_CONTAINER(frame), slider);
    
    c->slider = slider;
    return win;
}  

static gboolean
icon_clicked(GtkWidget *widget, GdkEventButton *event, backlight_priv *c)
{
    int brightness;

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
    /*
    if (!(event->type == GDK_BUTTON_PRESS && event->button == 2)) {
        run_app(c->mixer);
        RET(FALSE);
    }
    */
    if (c->muted) {
        brightness = c->muted_bright;
    } else {
        c->muted_bright = c->brightness;
        brightness = 0;
    }
    c->muted = !c->muted;
    set_brightness(c, brightness);
    brightness_update_gui(c);
    RET(FALSE);
}

static gboolean
icon_scrolled(GtkWidget *widget, GdkEventScroll *event, backlight_priv *c)
{
    int brightness;
    
    ENTER;
    brightness = (c->muted) ? c->muted_bright : ((meter_priv *) c)->level;
    brightness += 2 * ((event->direction == GDK_SCROLL_UP
            || event->direction == GDK_SCROLL_LEFT) ? 1 : -1);
    
    if (brightness > 100)
        brightness = 100;
    if (brightness < 0)
        brightness = 0;
    
	if (c->muted)
		c->muted_bright = brightness;
	else {
    	set_brightness(c, brightness);
    	brightness_update_gui(c);
    }
    RET(TRUE);
}

static gboolean
leave_cb(backlight_priv *c)
{
    ENTER;
    c->leave_id = 0;
    c->has_pointer = 0;
    gtk_widget_destroy(c->slider_window);
    c->slider_window = NULL;
    RET(FALSE);
}

static gboolean
crossed(GtkWidget *widget, GdkEventCrossing *event, backlight_priv *c)
{
    ENTER;
    if (event->type == GDK_ENTER_NOTIFY)
        c->has_pointer++;
    else
        c->has_pointer--;
    if (c->has_pointer > 0) {
        if (c->leave_id) {
            g_source_remove(c->leave_id);
            c->leave_id = 0;
        }
    } else {
        if (!c->leave_id && c->slider_window) {
            c->leave_id = g_timeout_add(1200, (GSourceFunc) leave_cb, c);
        }
    }
    DBG("has_pointer=%d\n", c->has_pointer);
    RET(FALSE);
}

static int
backlight_constructor(plugin_instance *p)
{
    backlight_priv *c;
    gchar *backlight;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (backlight_priv *) p;
    backlight = backlight_get_best_backlight();
    c->backlight_brightness = g_build_filename(backlight, "brightness", NULL);
    c->backlight_max_brightness = g_build_filename(backlight, "max_brightness", NULL);
    DBG("backlight: %s\n", c->backlight_brightness);
    if (c->backlight_brightness != NULL) {
        c->brightness = get_brightness(c);
    }
    if (c->backlight_max_brightness != NULL) {
        c->max_brightness = get_max_brightness(c);
    }
    //XCG(p->xc, "mixer", &c->mixger, str);
    //DBG("mixer=%s\n", c->mixer);
    //if (! asound_initialize(c)) {
    //    ERR("volume2:Not initialize sound card\n");
    //    RET(0);
    //}
    
    k->set_icons(&c->meter, names);
    c->update_id = g_timeout_add(1000, (GSourceFunc) brightness_update_gui, c);
    //c->vol = 200;
    brightness_update_gui(c);
    g_signal_connect(G_OBJECT(p->pwid), "scroll-event",
        G_CALLBACK(icon_scrolled), (gpointer) c);
    g_signal_connect(G_OBJECT(p->pwid), "button_press_event",
        G_CALLBACK(icon_clicked), (gpointer)c);
    g_signal_connect(G_OBJECT(p->pwid), "enter-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    g_signal_connect(G_OBJECT(p->pwid), "leave-notify-event",
        G_CALLBACK(crossed), (gpointer)c);

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
    //snd_mixer_close(c->handle);
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
