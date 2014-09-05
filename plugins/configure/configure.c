#include <stdlib.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "gtkbgbox.h"
#define DEBUGPRN
#include "dbg.h"

extern int config;

typedef struct {
    plugin_instance plugin;
    GdkPixmap *pix;
    GdkBitmap *mask;
} configure_priv;


static gint
clicked (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    configure_priv *wc = (configure_priv *) data;

    ENTER;
    if (event->type != GDK_BUTTON_PRESS)
        RET(FALSE);

    if (event->button == 1) {
		config=1;
        gtk_main_quit();
    } 
    RET(FALSE);
}

static void
configure_destructor(plugin_instance *p)
{
    configure_priv *wc = (configure_priv *)p;

    ENTER;
    DBG("configure:config=%d\n", config);
    if (wc->mask)
        g_object_unref(wc->mask);
    if (wc->pix)
        g_object_unref(wc->pix);
    RET();
}

static int
configure_constructor(plugin_instance *p)
{
    gchar *tooltip, *fname, *iname;
    configure_priv *wc;
    GtkWidget *button;
    int w, h;

    ENTER;
    wc = (configure_priv *) p;
    tooltip = fname = iname = NULL;
    XCG(p->xc, "Icon", &iname, str);
    XCG(p->xc, "Image", &fname, str);
    XCG(p->xc, "tooltip", &tooltip, str);
    fname = expand_tilda(fname);
    
    if (p->panel->orientation == GTK_ORIENTATION_HORIZONTAL) {
        w = -1;
        h = p->panel->max_elem_height;
    } else {
        w = p->panel->max_elem_height;
        h = -1;
    }
    button = fb_button_new(iname, fname, w, h, 0x202020, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(button), 0);
    g_signal_connect(G_OBJECT(button), "button_press_event",
          G_CALLBACK(clicked), (gpointer)wc);
  
    gtk_widget_show(button);
    gtk_container_add(GTK_CONTAINER(p->pwid), button);
    if (p->panel->transparent) 
        gtk_bgbox_set_background(button, BG_INHERIT,
            p->panel->tintcolor, p->panel->alpha);
    
    g_free(fname);
    if (tooltip) 
        gtk_widget_set_tooltip_markup(button, tooltip);

    RET(1);
}


static plugin_class class = {
    .count       = 0,
    .type        = "configure",
    .name        = "Configure Panel",
    .version     = "1.0",
    .description = "Configigure FBPanel",
    .priv_size   = sizeof(configure_priv),
    

    .constructor = configure_constructor,
    .destructor = configure_destructor,
};
static plugin_class *class_ptr = (plugin_class *) &class;
