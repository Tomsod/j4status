/*
 * j4status - Status line generator
 *
 * Copyright © 2012-2018 Quentin "Sardem FF7" Glidic
 *
 * This file is part of j4status.
 *
 * j4status is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * j4status is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with j4status. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <glib.h>
#include <upower.h>

#include <j4status-plugin-input.h>

struct _J4statusPluginContext {
    J4statusCoreInterface *core;
    GList *sections;
    UpClient *up_client;
    gboolean started;
};

typedef struct {
    J4statusPluginContext *context;
    GObject *device;
    J4statusSection *section;
} J4statusUpowerSection;

static void
#if UP_CHECK_VERSION(0,99,0)
_j4status_upower_device_changed(GObject *device, GParamSpec *pspec, gpointer user_data)
#else /* ! UP_CHECK_VERSION(0,99,0) */
_j4status_upower_device_changed(GObject *device, gpointer user_data)
#endif /* ! UP_CHECK_VERSION(0,99,0) */
{
    J4statusUpowerSection *section = user_data;

    UpDeviceState device_state;
    gdouble percentage = -1;
    gint64 time = -1;
    const gchar *status = "Bat";
    J4statusState state = J4STATUS_STATE_NO_STATE;

    g_object_get(device, "percentage", &percentage, "state", &device_state, NULL);

    switch ( device_state )
    {
    case UP_DEVICE_STATE_LAST: /* Size placeholder */
    case UP_DEVICE_STATE_UNKNOWN:
        j4status_section_set_state(section->section, J4STATUS_STATE_UNAVAILABLE);
        j4status_section_set_value(section->section, g_strdup("No battery"));
        return;
    case UP_DEVICE_STATE_EMPTY:
        status = "Empty";
        state = J4STATUS_STATE_BAD | J4STATUS_STATE_URGENT;
    break;
    case UP_DEVICE_STATE_FULLY_CHARGED:
        status = "Full";
        state = J4STATUS_STATE_GOOD;
    break;
    case UP_DEVICE_STATE_CHARGING:
    case UP_DEVICE_STATE_PENDING_CHARGE:
        status = "Chr";
        state = J4STATUS_STATE_AVERAGE;

        g_object_get(device, "time-to-full", &time, NULL);
    break;
    case UP_DEVICE_STATE_DISCHARGING:
    case UP_DEVICE_STATE_PENDING_DISCHARGE:
        status = "Bat";
        if ( percentage < 15 )
            state = J4STATUS_STATE_BAD;
        else
            state = J4STATUS_STATE_AVERAGE;

        if ( percentage < 5 )
            state |= J4STATUS_STATE_URGENT;

        g_object_get(device, "time-to-empty", &time, NULL);
    break;
    }
    j4status_section_set_state(section->section, state);

    gchar *value;
    if ( percentage < 0 )
        value = g_strdup_printf("%s", status);
    else if ( time < 1 )
        value = g_strdup_printf("%s %.02f%%", status, percentage);
    else
    {
        guint64 h = 0;
        guint64 m = 0;

        if ( time > 3600 )
        {
            h = time / 3600;
            time %= 3600;
        }
        if ( time > 60 )
        {
            m = time / 60;
            time %= 60;
        }

        value = g_strdup_printf("%s %.02f%% (%02ju:%02ju:%02jd)", status, percentage, h, m, time);
    }
    j4status_section_set_value(section->section, value);
}

static void
_j4status_upower_section_free(gpointer data)
{
    J4statusUpowerSection *section = data;

    j4status_section_free(section->section);

    g_object_unref(section->device);

    g_free(section);
}

static void
_j4status_upower_section_new(J4statusPluginContext *context, GObject *device, gboolean all_devices)
{
    UpDeviceKind kind;

    g_object_get(device, "kind", &kind, NULL);

    const gchar *path;
    const gchar *name = NULL;
    const gchar *instance = NULL;
    const gchar *label = NULL;

    path = up_device_get_object_path(UP_DEVICE(device));
    instance = g_utf8_strrchr(path, -1, '/') + strlen("/") + strlen(up_device_kind_to_string(kind)) + strlen("_");

    switch ( kind )
    {
    case UP_DEVICE_KIND_BATTERY:
        name = "upower-battery";
    break;
    case UP_DEVICE_KIND_UPS:
        if ( ! all_devices )
            return;
        name = "upower-ups";
        label = "UPS";
    break;
    case UP_DEVICE_KIND_MONITOR:
        if ( ! all_devices )
            return;
        name = "upower-monitor";
        label = "Monitor";
    break;
    case UP_DEVICE_KIND_MOUSE:
        if ( ! all_devices )
            return;
        name = "upower-mouse";
        label = "Mouse";
    break;
    case UP_DEVICE_KIND_KEYBOARD:
        if ( ! all_devices )
            return;
        name = "upower-keyboard";
        label = "Keyboard";
    break;
    case UP_DEVICE_KIND_PDA:
        if ( ! all_devices )
            return;
        name = "upower-pda";
        label = "PDA";
    break;
    case UP_DEVICE_KIND_PHONE:
        if ( ! all_devices )
            return;
        name = "upower-phone";
        label = "Phone";
    break;
    case UP_DEVICE_KIND_MEDIA_PLAYER:
        if ( ! all_devices )
            return;
        name = "upower-media-player";
        label = "Media player";
    break;
    case UP_DEVICE_KIND_TABLET:
        if ( ! all_devices )
            return;
        name = "upower-tablet";
        label = "Tablet";
    break;
    case UP_DEVICE_KIND_COMPUTER:
        if ( ! all_devices )
            return;
        name = "upower-computer";
        label = "Computer";
    break;
    case UP_DEVICE_KIND_UNKNOWN:
    case UP_DEVICE_KIND_LINE_POWER:
    case UP_DEVICE_KIND_LAST: /* Size placeholder */
        return;
    }

    J4statusUpowerSection *section;
    section = g_new0(J4statusUpowerSection, 1);
    section->context = context;
    section->device = g_object_ref(device);
    section->section = j4status_section_new(context->core);

    j4status_section_set_name(section->section, name);
    j4status_section_set_instance(section->section, instance);
    if ( label != NULL )
        j4status_section_set_label(section->section, label);
    j4status_section_set_max_width(section->section, -strlen("Chr 100.0% (00:00:00)"));

    if ( j4status_section_insert(section->section) )
    {
        context->sections = g_list_prepend(context->sections, section);

#if UP_CHECK_VERSION(0,99,0)
        g_signal_connect(device, "notify", G_CALLBACK(_j4status_upower_device_changed), section);
        _j4status_upower_device_changed(device, NULL, section);
#else /* ! UP_CHECK_VERSION(0,99,0) */
        g_signal_connect(device, "changed", G_CALLBACK(_j4status_upower_device_changed), section);
        _j4status_upower_device_changed(device, section);
#endif /* ! UP_CHECK_VERSION(0,99,0) */
    }
    else
        _j4status_upower_section_free(section);
}

static void _j4status_upower_uninit(J4statusPluginContext *context);

static J4statusPluginContext *
_j4status_upower_init(J4statusCoreInterface *core)
{
    J4statusPluginContext *context;
    context = g_new0(J4statusPluginContext, 1);
    context->core = core;

    context->up_client = up_client_new();

#if ! UP_CHECK_VERSION(0,99,0)
    if ( ! up_client_enumerate_devices_sync(context->up_client, NULL, NULL) )
    {
        g_object_unref(context->up_client);
        g_free(context);
        return NULL;
    }
#endif /* ! UP_CHECK_VERSION(0,99,0) */

    gboolean all_devices = FALSE;

    GKeyFile *key_file;
    key_file = j4status_config_get_key_file("UPower");
    if ( key_file != NULL )
    {
        all_devices = g_key_file_get_boolean(key_file, "UPower", "AllDevices", NULL);
        g_key_file_free(key_file);
    }

    GPtrArray *devices;
    guint i;

    devices = up_client_get_devices(context->up_client);
    if ( devices == NULL )
    {
        g_warning("No devices to monitor, aborting");
        g_object_unref(context->up_client);
        g_free(context);
        return NULL;
    }
    for ( i = 0 ; i < devices->len ; ++i )
        _j4status_upower_section_new(context, g_ptr_array_index(devices, i), all_devices);
    g_ptr_array_unref(devices);

    if ( context->sections == NULL )
    {
        g_message("Missing configuration: No device to monitor, aborting");
        _j4status_upower_uninit(context);
        return NULL;
    }

    return context;
}

static void
_j4status_upower_uninit(J4statusPluginContext *context)
{
    g_list_free_full(context->sections, _j4status_upower_section_free);

    g_free(context);
}

static void
_j4status_upower_start(J4statusPluginContext *context)
{
    context->started = TRUE;
}

static void
_j4status_upower_stop(J4statusPluginContext *context)
{
    context->started = FALSE;
}

void
j4status_input_plugin(J4statusInputPluginInterface *interface)
{
    libj4status_input_plugin_interface_add_init_callback(interface, _j4status_upower_init);
    libj4status_input_plugin_interface_add_uninit_callback(interface, _j4status_upower_uninit);

    libj4status_input_plugin_interface_add_start_callback(interface, _j4status_upower_start);
    libj4status_input_plugin_interface_add_stop_callback(interface, _j4status_upower_stop);
}
