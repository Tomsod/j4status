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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#include <glib.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <j4status-plugin-input.h>

/*
 * We use either the PulseAudio format
 * ore a more compact one.
 */
#define J4STATUS_PULSEAUDIO_VOLUME_SPRINTF_MAX PA_CVOLUME_SNPRINT_MAX

#define J4STATUS_PULSEAUDIO_VOLUME_TO_PERCENT(v) ( ( (v) * 100.0 ) / PA_VOLUME_NORM )
#define J4STATUS_PULSEAUDIO_VOLUME_FROM_PERCENT(v) ( ( (v) / 100.0 ) * PA_VOLUME_NORM )

#define J4STATUS_PULSEAUDIO_DEFAULT_FORMAT "${volume[@% ]}%"

typedef enum {
    TOKEN_PORT,
    TOKEN_MUTE,
    TOKEN_VOLUME,
} J4statusPulseaudioFormatToken;

static const gchar * const _j4status_pulseaudio_tokens[] = {
    [TOKEN_PORT]   = "port",
    [TOKEN_MUTE]   = "mute",
    [TOKEN_VOLUME] = "volume",
};

typedef enum {
    TOKEN_FLAG_PORT   = (1 << TOKEN_PORT),
    TOKEN_FLAG_MUTE   = (1 << TOKEN_MUTE),
    TOKEN_FLAG_VOLUME = (1 << TOKEN_VOLUME),
} J4statusPulseaudioFormatTokenFlag;

typedef struct {
    J4statusFormatString *format;
    GHashTable *actions;
    pa_volume_t increment;
    pa_volume_t volume;
    gboolean unlimited_volume;
} J4statusPulseaudioConfig;

struct _J4statusPluginContext {
    J4statusCoreInterface *core;
    J4statusPulseaudioConfig config;
    pa_context *context;
    pa_glib_mainloop *pa_loop;
    GHashTable *sections;
};

typedef enum {
    ACTION_RAISE,
    ACTION_LOWER,
    ACTION_SET,
    ACTION_MUTE_TOGGLE,
    ACTION_MUTE_SET,
    ACTION_MUTE_UNSET,
} J4statusPulseaudioAction;

static const gchar * const _j4status_pulseaudio_actions[] = {
    [ACTION_RAISE]       = "raise",
    [ACTION_LOWER]       = "lower",
    [ACTION_SET]         = "set",
    [ACTION_MUTE_TOGGLE] = "mute toggle",
    [ACTION_MUTE_SET]    = "mute set",
    [ACTION_MUTE_UNSET]  = "mute unset",
};

typedef enum {
    PORT_SPEAKER = 0,
    PORT_HEADPHONES,
} J4statusPulseaudioPort;

typedef struct {
    gboolean mute;
    J4statusPulseaudioPort port;
    pa_cvolume volume;
} J4statusPulseaudioFormatData;

typedef struct {
    J4statusPluginContext *context;
    J4statusSection *section;
    guint32 index;
    pa_cvolume volume;
    gboolean mute;
} J4statusPulseaudioSection;


static void _j4status_pulseaudio_section_action_callback(J4statusSection *section_, const gchar *action_, gpointer user_data);

static J4statusPulseaudioSection *
_j4status_pulseaudio_section_get(J4statusPluginContext *context, const pa_sink_info *i)
{
    J4statusPulseaudioSection *section;

    section = g_hash_table_lookup(context->sections, GUINT_TO_POINTER(i->index));
    if ( section != NULL )
        return section;

    section = g_new0(J4statusPulseaudioSection, 1);
    section->context = context;

    section->section = j4status_section_new(context->core);
    j4status_section_set_name(section->section, "pulseaudio");
    j4status_section_set_instance(section->section, i->name);

    if ( context->config.actions != NULL )
        j4status_section_set_action_callback(section->section, _j4status_pulseaudio_section_action_callback, section);

    if ( ! j4status_section_insert(section->section) )
    {
        j4status_section_free(section->section);
        g_free(section);
        return NULL;
    }

    section->index = i->index;

    g_hash_table_insert(context->sections, GUINT_TO_POINTER(i->index), section);

    return section;
}

static void
_j4status_pulseaudio_section_free(gpointer data)
{
    J4statusPulseaudioSection *section = data;

    j4status_section_free(section->section);

    g_free(section);
}

static GVariant *
_j4status_pulseaudio_format_callback(const gchar *token, guint64 value, gconstpointer user_data)
{
    const J4statusPulseaudioFormatData *data = user_data;

    switch ( (J4statusPulseaudioFormatToken) value )
    {
    case TOKEN_MUTE:
        return g_variant_new_boolean(data->mute);
    case TOKEN_VOLUME:
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("at"));
        guint8 c;
        for ( c = 0 ; c < data->volume.channels ; ++c )
            g_variant_builder_add_value(&builder, g_variant_new_uint64(J4STATUS_PULSEAUDIO_VOLUME_TO_PERCENT(data->volume.values[c])));
        return g_variant_builder_end(&builder);
    }
    case TOKEN_PORT:
        return g_variant_new_byte(data->port);
    }
    return NULL;
}

static void
_j4status_pulseaudio_sink_info_callback(pa_context *con, const pa_sink_info *i, int eol, void *user_data)
{
    J4statusPluginContext *context = user_data;

    if ( eol )
        return;

    J4statusPulseaudioSection *section;
    section = _j4status_pulseaudio_section_get(context, i);
    if ( section == NULL )
        return;

    pa_cvolume_set(&section->volume, i->volume.channels, PA_VOLUME_MUTED);
    pa_cvolume_merge(&section->volume, &section->volume, &i->volume);
    section->mute = i->mute;

    guint8 c;
    pa_volume_t vol = i->volume.values[0];
    for ( c = 1 ; c < i->volume.channels ; ++c )
    {
        if ( vol != i->volume.values[c] )
            break;
    }

    J4statusState state = J4STATUS_STATE_NO_STATE;
    gchar *value;

    if ( section->mute )
        state = J4STATUS_STATE_BAD;
    else
        state = J4STATUS_STATE_GOOD;

    J4statusPulseaudioFormatData data = {
        .mute = section->mute,
        .port = PORT_SPEAKER,
        .volume = i->volume,
    };

    if ( i->active_port != NULL )
    {
        if ( g_str_has_suffix(i->active_port->name, "-headphones") )
            data.port = PORT_HEADPHONES;
    }

    /*
     * We walked through the whole list and
     * all channels are sharing the same volume
     */
    if ( c == i->volume.channels )
        data.volume.channels = 1;

    value = j4status_format_string_replace(context->config.format, _j4status_pulseaudio_format_callback, &data);

    j4status_section_set_state(section->section, state);
    j4status_section_set_value(section->section, value);
}

static void
_j4status_pulseaudio_context_state_callback(pa_context *con, void *user_data)
{
    J4statusPluginContext *context = user_data;
    pa_context_state_t state = pa_context_get_state(context->context);
    pa_operation *op;

    switch ( state )
    {
    case PA_CONTEXT_FAILED:
        g_warning("Connection to PulseAudio failed: %s", g_strerror(pa_context_errno(context->context)));
    break;
    case PA_CONTEXT_READY:
        op = pa_context_subscribe(context->context, PA_SUBSCRIPTION_MASK_SINK, NULL, user_data);
        if ( op != NULL )
            pa_operation_unref(op);
        op = pa_context_get_sink_info_list(context->context, _j4status_pulseaudio_sink_info_callback, user_data);
        if ( op != NULL )
            pa_operation_unref(op);
    break;
    case PA_CONTEXT_TERMINATED:
    break;
    default:
    break;
    }
}

static void
_j4status_pulseaudio_section_success_callback(pa_context *con, gboolean success, gpointer user_data)
{
    J4statusPulseaudioSection *section = user_data;

    pa_context_get_sink_info_by_index(con, section->index, _j4status_pulseaudio_sink_info_callback, section->context);
}

static void
_j4status_pulseaudio_section_action_callback(J4statusSection *section_, const gchar *action_, gpointer user_data)
{
    J4statusPulseaudioSection *section = user_data;
    J4statusPluginContext *context = section->context;

    J4statusPulseaudioAction action;
    if ( ! g_hash_table_lookup_extended(context->config.actions, action_, NULL, (gpointer *)&action) )
        return;

    gboolean set_volume = FALSE;
    gboolean set_mute = FALSE;

    gboolean mute;
    pa_volume_t volume = pa_cvolume_max(&section->volume);

    switch ( action )
    {
    case ACTION_RAISE:
        set_volume = TRUE;
        if ( context->config.unlimited_volume )
            volume += MIN(context->config.increment, PA_VOLUME_MAX - volume);
        else
            volume += MIN(context->config.increment, PA_VOLUME_NORM - volume);
    break;
    case ACTION_LOWER:
        set_volume = TRUE;
        volume -= MIN(context->config.increment, volume - PA_VOLUME_MUTED);
    break;
    case ACTION_SET:
        set_volume = TRUE;
        volume = context->config.volume;
    break;
    case ACTION_MUTE_TOGGLE:
        set_mute = TRUE;
        mute = !section->mute;
    break;
    case ACTION_MUTE_SET:
        set_mute = TRUE;
        mute = TRUE;
    break;
    case ACTION_MUTE_UNSET:
        set_mute = TRUE;
        mute = FALSE;
    break;
    }

    if ( set_volume )
    {
        pa_cvolume cvolume;

        pa_cvolume_set(&cvolume, section->volume.channels, PA_VOLUME_MUTED);
        pa_cvolume_merge(&cvolume, &cvolume, &section->volume);
        pa_cvolume_scale(&cvolume, volume);

        pa_context_set_sink_volume_by_index(context->context, section->index, &cvolume, _j4status_pulseaudio_section_success_callback, section);
    }

    if ( set_mute )
        pa_context_set_sink_mute_by_index(context->context, section->index, mute, _j4status_pulseaudio_section_success_callback, section);
}

static void
_j4status_pulseaudio_context_event_callback(pa_context *con, pa_subscription_event_type_t t, uint32_t idx, void *user_data)
{
    J4statusPluginContext *context = user_data;

    pa_operation *op;


    switch ( t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK )
    {
    case PA_SUBSCRIPTION_EVENT_SINK:
        switch ( t & PA_SUBSCRIPTION_EVENT_TYPE_MASK )
        {
        case PA_SUBSCRIPTION_EVENT_NEW:
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            op = pa_context_get_sink_info_by_index(con, idx, _j4status_pulseaudio_sink_info_callback, context);
            if ( op != NULL )
                pa_operation_unref(op);
        break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            g_hash_table_remove(context->sections, GUINT_TO_POINTER(idx));
        break;
        default:
        break;
        }
    break;
    default:
        g_assert_not_reached();
    }
}

static void _j4status_pulseaudio_uninit(J4statusPluginContext *context);

static J4statusPluginContext *
_j4status_pulseaudio_init(J4statusCoreInterface *core)
{
    pa_glib_mainloop *pa_loop;
    pa_context *pa_context_;

    pa_loop = pa_glib_mainloop_new(NULL);

    pa_context_ = pa_context_new(pa_glib_mainloop_get_api(pa_loop), PACKAGE_NAME " pulseaudio plugin");
    if ( pa_context_ == NULL )
    {
        g_warning("Couldn't open sound system");
        pa_glib_mainloop_free(pa_loop);
        return NULL;
    }

    J4statusPluginContext *context;

    context = g_new0(J4statusPluginContext, 1);
    context->core = core;

    context->pa_loop = pa_loop;
    context->context = pa_context_;

    J4statusPulseaudioConfig config = {
        .actions = NULL,
        .increment = J4STATUS_PULSEAUDIO_VOLUME_FROM_PERCENT(5),
        .volume = J4STATUS_PULSEAUDIO_VOLUME_FROM_PERCENT(100),
        .unlimited_volume = FALSE,
    };
    gchar *format = NULL;

    GKeyFile *key_file;
    key_file = j4status_config_get_key_file("PulseAudio");
    if ( key_file != NULL )
    {
        gint64 value;

        value = g_key_file_get_int64(key_file, "PulseAudio", "Increment", NULL);
        if ( ( value > 0 ) && ( value < 100 ) )
            config.increment = J4STATUS_PULSEAUDIO_VOLUME_FROM_PERCENT(value);

        value = g_key_file_get_int64(key_file, "PulseAudio", "Volume", NULL);
        if ( ( value > 0 ) && ( value <= 100 ) )
            config.volume = J4STATUS_PULSEAUDIO_VOLUME_FROM_PERCENT(value);

        config.unlimited_volume = g_key_file_get_boolean(key_file, "PulseAudio", "UnlimitedVolume", NULL);

        format = g_key_file_get_string(key_file, "PulseAudio", "Format", NULL);
        config.actions = j4status_config_key_file_get_actions(key_file, "PulseAudio", _j4status_pulseaudio_actions, G_N_ELEMENTS(_j4status_pulseaudio_actions));

        g_key_file_free(key_file);
    }

    config.format = j4status_format_string_parse(format, _j4status_pulseaudio_tokens, G_N_ELEMENTS(_j4status_pulseaudio_tokens), J4STATUS_PULSEAUDIO_DEFAULT_FORMAT, NULL);

    context->config = config;

    pa_context_set_state_callback(context->context, _j4status_pulseaudio_context_state_callback, context);
    pa_context_set_subscribe_callback(context->context, _j4status_pulseaudio_context_event_callback, context);

    context->sections = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, _j4status_pulseaudio_section_free);

    pa_context_connect(context->context, NULL, 0, NULL);

    return context;
}

static void
_j4status_pulseaudio_uninit(J4statusPluginContext *context)
{
    pa_context_disconnect(context->context);

    g_hash_table_unref(context->sections);

    pa_context_unref(context->context);
    pa_glib_mainloop_free(context->pa_loop);

    j4status_format_string_unref(context->config.format);

    g_free(context);
}

J4STATUS_EXPORT void
j4status_input_plugin(J4statusInputPluginInterface *interface)
{
    libj4status_input_plugin_interface_add_init_callback(interface, _j4status_pulseaudio_init);
    libj4status_input_plugin_interface_add_uninit_callback(interface, _j4status_pulseaudio_uninit);
}
