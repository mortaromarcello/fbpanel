#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

#define LEN 100
#define PROC_ACPI "/proc/acpi/battery/"

int file_exist(const char *path)
{
    if (access(path, F_OK) != -1)
        return 1;
    else
        return 0;
}

static gboolean
get_token_eq(gchar *buf, gchar *token, gchar *value, gboolean *ret)
{
    int len;
    gchar *var;

    ENTER;
    len = strlen(token);
    if (!(var = strstr(buf, token)))
        RET(FALSE);
    for (var = var + len; isspace(*var); var++) ;
    *ret = !strncmp(var, value, strlen(value));
    RET(TRUE);
}

static gboolean
get_token_int(gchar *buf, gchar *token, gint *value)
{
    int len;
    gchar *var;

    ENTER;
    len = strlen(token);
    if (!(var = strstr(buf, token)))
        RET(FALSE);
    for (var = var + len; isspace(*var); var++) ;
    if (sscanf(var, "%d", value) == 1)
        RET(TRUE);
    RET(FALSE);
}

static gboolean
read_proc(battery_priv *c, GString *path)
{
    int len, lfcap, rcap;
    gchar *buf;
    gboolean ret, exist, charging;

    ENTER;
    len = path->len;

    g_string_append(path, "/info");
    ret = g_file_get_contents(path->str, &buf, 0, NULL);
    DBG("reading %s %s\n", path->str, ret ? "ok" : "fail");
    g_string_truncate(path, len);
    if (!ret)
        RET(FALSE);
    ret = get_token_eq(buf, "present:", "yes", &exist)
        && exist && get_token_int(buf, "last full capacity:", &lfcap);

    g_free(buf);
    if (!ret)
        RET(FALSE);

    g_string_append(path, "/state");
    ret = g_file_get_contents(path->str, &buf, 0, NULL);
    DBG("reading %s %s\n", path->str, ret ? "ok" : "fail");
    g_string_truncate(path, len);
    if (!ret)
        RET(FALSE);
    ret = get_token_eq(buf, "present:", "yes", &exist)
        && exist
        && get_token_int(buf, "remaining capacity:", &rcap)
        && get_token_eq(buf, "charging state:", "charging", &charging);
    g_free(buf);
    if (!ret)
        RET(FALSE);
    DBG("battery=%s\nlast full capacity=%d\nremaining capacity=%d\n"
        "charging=%d\n",
        path->str, lfcap, rcap, charging);

    if (!(lfcap >= rcap && lfcap > 0 && rcap >= 0))
        RET(FALSE);

    c->exist = TRUE;
    c->charging = charging;
    c->level = (int) ((gfloat) rcap * 100 / (gfloat) lfcap);
    RET(TRUE);
}

//
static gboolean
read_sys(battery_priv *c)
{
    DIR *dir;
    struct dirent *dirent;
    FILE *file;

    char filename[256];
    c->exist = FALSE;

    dir = opendir("/sys/class/power_supply");
    if (!dir) {
        DBG("can't open /sys/class/power_supply/\n");
        RET(FALSE);
    }

    while ((dirent = readdir(dir))) {
        double charge_now = 0.0;
        double charge_full = 0.0;
        char line[1024];
        file = NULL;

        if (strstr(dirent->d_name, "AC") || strstr(dirent->d_name, "ACAD"))
            continue;
        sprintf(filename, "/sys/class/power_supply/%s/present", dirent->d_name);
        file = fopen(filename, "r");
        if (!file)
            continue;
        int s;
        if ((s = getc(file)) != EOF) {
            if (s == 0)
                break;
            else
                c->exist = TRUE;
            DBG("exist:%s\n", (c->exist) ? "Yes" : "No");
        }
        fclose(file);

        sprintf(filename, "/sys/class/power_supply/%s/status", dirent->d_name);
        file = fopen(filename, "r");
        if (!file)
            continue;
        memset(line, 0, 1024);
        if (fgets(line, 1024, file) != NULL) {
            if (!strstr(line, "Discharging"))
                c->charging = TRUE;
            else
                c->charging = FALSE;
            DBG("status:%d\n", c->charging);
        }
        fclose(file);

        sprintf(filename, "/sys/class/power_supply/%s/charge_full", dirent->d_name);
        if (file_exist(filename))
            file = fopen(filename, "r");
        else
        {
            sprintf(filename, "/sys/class/power_supply/%s/energy_full", dirent->d_name);
            if (file_exist(filename))
                file = fopen(filename, "r");
        }
        if (!file)
            continue;
        memset(line, 0, 1024);
        if (fgets(line, 1024, file) != NULL) {
            charge_full = strtoull(line, NULL, 10) / 1000.0;
            DBG("charge_full:%g\n", charge_full);
        }
        fclose(file);
        
        sprintf(filename, "/sys/class/power_supply/%s/charge_now", dirent->d_name);
        if (file_exist(filename))
            file = fopen(filename, "r");
        else
        {
            sprintf(filename, "/sys/class/power_supply/%s/energy_now", dirent->d_name);
            if (file_exist(filename))
                file = fopen(filename, "r");
        }
        if (!file)
            continue;
        memset(line, 0, 1024);
        if (fgets(line, 1024, file) != NULL) {
            charge_now = strtoull(line, NULL, 10) / 1000.0;
            DBG("charge_now:%g\n", charge_now);
        }
        fclose(file);

        c->level = (int)(charge_now * 100 / charge_full);
        c->charge_now = charge_now;
        c->charge_full = charge_full;
        DBG("percent:%g\n", c->level);
    }
    closedir(dir);
    RET(TRUE);
}

static gboolean
battery_update_os_proc(battery_priv *c)
{
    GString *path;
    int len;
    GDir *dir;
    gboolean ret = FALSE;
    const gchar *file;

    ENTER;
    c->exist = FALSE;
    path = g_string_sized_new(200);
    g_string_append(path, PROC_ACPI);
    len = path->len;
    if (!(dir = g_dir_open(path->str, 0, NULL))) {
        DBG("can't open dir %s\n", path->str);
        goto out;
    }
    while (!ret && (file = g_dir_read_name(dir))) {
        g_string_append(path, file);
        DBG("testing %s\n", path->str);
        ret = g_file_test(path->str, G_FILE_TEST_IS_DIR);
        if (ret)
            ret = read_proc(c, path);
        g_string_truncate(path, len);
    }
    g_dir_close(dir);

out:
    g_string_free(path, TRUE);
    RET(ret);
}

static gboolean
battery_update_os_sys(battery_priv *c)
{
    gboolean ret = FALSE;

    ENTER;
    ret = read_sys(c);
    RET(ret);
}

static gboolean
battery_update_os(battery_priv *c)
{
    ENTER;
    RET(battery_update_os_proc(c) || battery_update_os_sys(c));
}
