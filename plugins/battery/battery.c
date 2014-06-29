#include "misc.h"
#include "../meter/meter.h"
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libacpi.h>

#define DEBUGPRN
#include "dbg.h"

typedef struct {
    meter_priv meter;
    int timer;
    int level;
    global_t global;
    int num_batt;
    gboolean charging;
    gboolean exist;
} battery_priv;

static meter_class *k;

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

static void
battery_update_os(battery_priv *c)
{
    init_acpi_batt(&c->global);
    if (read_acpi_batt(c->num_batt) == ITEM_EXCEED) {
        DBG("battery: battery not found\n");
        RET(0);
    }
    c->exist = batteries[1].present;
    c->charging = (batteries[1].charge_state == C_CHARGE) ? 1 : 0;
    c->level = batteries[1].percentage;
}

static gboolean
battery_update(battery_priv *c)
{
    gchar buf[50];
    gchar **i;

    ENTER;
    battery_update_os(c);
    DBG("battery: level=%d\n", c->level);
    if (c->exist) {
        i = c->charging ? batt_charging : batt_working;
        //g_snprintf(buf, sizeof(buf), "<b>Battery:</b> %d%%%s", (int) c->level, c->charging ? "\nCharging" : "");
        g_snprintf(buf, sizeof(buf), "<b>Battery:</b> %d%%", (int) c->level);
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, buf);
    } else {
        i = batt_na;
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid,
            "Runing on AC\nNo battery found");
    }
    k->set_icons(&c->meter, i);
    k->set_level(&c->meter, c->level);
    RET(TRUE);
}


static int
battery_constructor(plugin_instance *p)
{
    battery_priv *c;
    int i;
    
    ENTER;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (battery_priv *) p;
    if (check_acpi_support() == NOT_SUPPORTED) {
        DBG("battery: acpi not  supported\n");
        RET(0);
    }
    if (init_acpi_batt(&c->global) == NOT_SUPPORTED) {
        DBG("battery: battery not supported\n");
        RET(0);
    }
    for (i = 0; i < c->global.batt_count; i++) {
        read_acpi_batt(i);
        if (! strcmp(batteries[i].name, "BAT1")) {
            c->num_batt = i;
            break;
        }
    }
    DBG("battery constructor\n");
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
    .type        = "battery",
    .name        = "battery usage",
    .version     = "1.0",
    .description = "Display battery usage",
    .priv_size   = sizeof(battery_priv),
    .constructor = battery_constructor,
    .destructor  = battery_destructor,
};

static plugin_class *class_ptr = (plugin_class *) &class;
