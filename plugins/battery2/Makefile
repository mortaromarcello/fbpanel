## miniconf makefiles ## 1.1 ##

TOPDIR := ../..

battery2_src = battery2.c batt_sys.c
battery2_cflags = -DPLUGIN $(GTK2_CFLAGS) 
battery2_libs = $(GTK2_LIBS) -lnotify
battery2_type = lib 

include $(TOPDIR)/.config/rules.mk
