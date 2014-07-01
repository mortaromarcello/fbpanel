#include "misc.h"
#include "../meter/meter.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEBUGPRN
#include "dbg.h"

static meter_class *k;

typedef struct {
    meter_priv meter;
    int timer;
    gfloat level;
    gboolean charging;
    gboolean exist;
    int show_icon_in_ac;
} battery_priv;

static gboolean battery_update_os(battery_priv *c);

static gchar *batt_working[] = {
    "battery_0",
    "battery_1",
    "battery_2",
    "battery_3",
    "battery_4",
    "battery_5",
    "battery_6",
    "battery_7",
    "battery_8",
    NULL
};

static gchar *batt_charging[] = {
    "battery_charging_0",
    "battery_charging_1",
    "battery_charging_2",
    "battery_charging_3",
    "battery_charging_4",
    "battery_charging_5",
    "battery_charging_6",
    "battery_charging_7",
    "battery_charging_8",
    NULL
};

static gchar *batt_na[] = {
    "battery_na",
    NULL
};

#if defined __linux__
#include "os_linux.c"
#else

static void
battery_update_os(battery_priv *c)
{
    c->exist = FALSE;
}

#endif

static gboolean
battery_update(battery_priv *c)
{
    gchar buf[50];
    gchar **i;

    ENTER;
    battery_update_os(c);
    if (c->exist) {
        i = c->charging ? batt_charging : batt_working;
        g_snprintf(buf, sizeof(buf), _("<b>Battery:</b> %d%%%s"),
            (int) c->level, c->charging ? _("\nCharging") : "");
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, buf);
        k->set_icons(&c->meter, i);
        k->set_level(&c->meter, c->level);
    } else {
        if (c->show_icon_in_ac) {
            i = batt_na;
            gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, _("Running on AC\nNo battery found"));
            k->set_icons(&c->meter, i);
            k->set_level(&c->meter, c->level);
        }
        else
            RET(FALSE);
    }

    RET(TRUE);
}


static int
battery_constructor(plugin_instance *p)
{
    battery_priv *c;

    ENTER;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (battery_priv *) p;
    c->show_icon_in_ac = 0;
    XCG(p->xc, "showiconinac", &c->show_icon_in_ac, enum, bool_enum);
    DBG("ShowIconInAC = %s\n", (c->show_icon_in_ac) ? "True":"False");
    c->timer = g_timeout_add(2000, (GSourceFunc) battery_update, c);
    battery_update(c);
    RET(1);
}

static void
battery_destructor(plugin_instance *p)
{
    battery_priv *c = (battery_priv *) p;

    ENTER;
    if (c->timer)
        g_source_remove(c->timer);
    PLUGIN_CLASS(k)->destructor(p);
    class_put("meter");
    RET();
}

static plugin_class class = {
    .count       = 0,
    .type        = "battery2",
    .name        = "battery usage",
    .version     = "1.0",
    .description = "Display battery usage",
    .priv_size   = sizeof(battery_priv),
    .constructor = battery_constructor,
    .destructor  = battery_destructor,
};

static plugin_class *class_ptr = (plugin_class *) &class;
