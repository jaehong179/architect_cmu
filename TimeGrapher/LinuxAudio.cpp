#include <QtGlobal>
#if defined(Q_OS_LINUX)
#include "LinuxAudio.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>



// Needs libasound2-dev - sudo apt-get install libasound2-dev

/*

    Set microphone volume and disable AGC using exact ALSA names.

    Arguments:

        argv[1] = exact ALSA card name
        argv[2] = exact mic volume element/control name
        argv[3] = volume percent, 0..100
        argv[4] = exact AGC element/control name

    Example:

        ./alsa_set_mic "USB Audio Device" "Mic" 75 "Auto Gain Control"

    Notes:

        This program supports both ALSA simple mixer names and raw ALSA
        control names.

        Simple mixer examples:

            "Mic"
            "Capture"
            "Internal Mic"

        Raw control examples:

            "Mic Capture Volume"
            "Capture Volume"
            "Auto Gain Control"
            "Mic Auto Gain Control"

        The card name must exactly match one of ALSA's reported card names,
        long names, IDs, mixer names, or driver names.
*/

static const char *elem_type_to_string(snd_ctl_elem_type_t type);
static void print_element_info(snd_ctl_t *ctl, snd_ctl_elem_id_t *id);
static void list_card_elements(int card_number);
static long percent_to_raw(long minv, long maxv, int percent);
static int find_card_by_exact_name(const char *card_name);
static int open_simple_mixer(int card, snd_mixer_t **mixer_out);
static snd_mixer_elem_t *find_simple_element_exact(snd_mixer_t *mixer,const char *element_name);
static int set_mic_volume_simple(int card,const char *mic_element_name,int percent);
static int set_mic_volume_raw_control(int card,const char *control_name,int percent);
static int disable_agc_simple(int card,const char *agc_element_name);
static int disable_agc_raw_control(int card,const char *control_name);

void LinuxSetSoundParameters(const char *card_name,const char *mic_name,const char *agc_name,int volume_percent)
{
    int card;
    int err_simple;
    int err_raw;


    if (volume_percent < 0 || volume_percent > 100) {
        fprintf(stderr, "Volume percent must be 0..100\n");
        return ;
    }

    card = find_card_by_exact_name(card_name);
    if (card < 0) {
        fprintf(stderr,
                "Could not find exact ALSA card name '%s'\n",
                card_name);
        return ;
    }

    printf("Using ALSA card hw:%d for exact card name '%s'\n",
           card,
           card_name);

    /*
        Try mic volume as a simple mixer element first.
        If that fails, try it as a raw ALSA control.
    */
    err_simple = set_mic_volume_simple(card, mic_name, volume_percent);

    if (err_simple < 0) {
        err_raw = set_mic_volume_raw_control(card, mic_name, volume_percent);

        if (err_raw < 0) {
            fprintf(stderr,
                    "Failed to set mic volume '%s'\n"
                    "  simple mixer error: %s\n"
                    "  raw control error:   %s\n",
                    mic_name,
                    snd_strerror(err_simple),
                    snd_strerror(err_raw));
            return ;
        }
    }

    /*
        Try AGC as a simple mixer switch first.
        If that fails, try it as a raw ALSA control.
    */
    err_simple = disable_agc_simple(card, agc_name);

    if (err_simple < 0) {
        err_raw = disable_agc_raw_control(card, agc_name);

        if (err_raw < 0) {
            fprintf(stderr,
                    "Failed to disable AGC '%s'\n"
                    "  simple mixer error: %s\n"
                    "  raw control error:   %s\n",
                    agc_name,
                    snd_strerror(err_simple),
                    snd_strerror(err_raw));
            return ;
        }
    }

    printf("Done.\n");
    return ;


}





static long percent_to_raw(long minv, long maxv, int percent)
{
    if (percent < 0)
        percent = 0;

    if (percent > 100)
        percent = 100;

    return minv + ((maxv - minv) * percent) / 100;
}

/*
    Finds a card by exact string match.

    The provided card_name is compared against:

        snd_ctl_card_info_get_id()
        snd_ctl_card_info_get_name()
        snd_ctl_card_info_get_longname()
        snd_ctl_card_info_get_mixername()
        snd_ctl_card_info_get_driver()

    Returns ALSA card index, such as 0, 1, 2, etc.
*/
static int find_card_by_exact_name(const char *card_name)
{
    int card = -1;
    int err;

    while ((err = snd_card_next(&card)) >= 0 && card >= 0) {
        char hw_name[32];
        snd_ctl_t *ctl = NULL;
        snd_ctl_card_info_t *info;

        snd_ctl_card_info_alloca(&info);

        snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

        err = snd_ctl_open(&ctl, hw_name, 0);
        if (err < 0)
            continue;

        err = snd_ctl_card_info(ctl, info);
        if (err == 0) {
            const char *id       = snd_ctl_card_info_get_id(info);
            const char *name     = snd_ctl_card_info_get_name(info);
            const char *longname = snd_ctl_card_info_get_longname(info);
            const char *mixer    = snd_ctl_card_info_get_mixername(info);
            const char *driver   = snd_ctl_card_info_get_driver(info);

            if ((id       && strcmp(card_name, id)       == 0) ||
                (name     && strcmp(card_name, name)     == 0) ||
                (longname && strcmp(card_name, longname) == 0) ||
                (mixer    && strcmp(card_name, mixer)    == 0) ||
                (driver   && strcmp(card_name, driver)   == 0)) {
                snd_ctl_close(ctl);
                return card;
            }
        }

        snd_ctl_close(ctl);
    }

    return -ENOENT;
}

static int open_simple_mixer(int card, snd_mixer_t **mixer_out)
{
    char hw_name[32];
    int err;

    snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

    err = snd_mixer_open(mixer_out, 0);
    if (err < 0)
        return err;

    err = snd_mixer_attach(*mixer_out, hw_name);
    if (err < 0)
        goto fail;

    err = snd_mixer_selem_register(*mixer_out, NULL, NULL);
    if (err < 0)
        goto fail;

    err = snd_mixer_load(*mixer_out);
    if (err < 0)
        goto fail;

    return 0;

fail:
    snd_mixer_close(*mixer_out);
    *mixer_out = NULL;
    return err;
}

/*
    Exact simple mixer element lookup.

    Example simple mixer names:

        "Mic"
        "Capture"
        "Internal Mic"
*/
static snd_mixer_elem_t *find_simple_element_exact(
    snd_mixer_t *mixer,
    const char *element_name
    )
{
    snd_mixer_elem_t *elem;

    for (elem = snd_mixer_first_elem(mixer);
         elem;
         elem = snd_mixer_elem_next(elem)) {

        const char *name;

        if (!snd_mixer_selem_is_active(elem))
            continue;

        name = snd_mixer_selem_get_name(elem);

        if (name && strcmp(name, element_name) == 0)
            return elem;
    }

    return NULL;
}

/*
    Set mic volume using ALSA simple mixer API.

    This works when the element name is something like:

        "Mic"
        "Capture"
        "Internal Mic"
*/
static int set_mic_volume_simple(
    int card,
    const char *mic_element_name,
    int percent
    )
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem;
    long minv;
    long maxv;
    long raw;
    int err;

    err = open_simple_mixer(card, &mixer);
    if (err < 0)
        return err;

    elem = find_simple_element_exact(mixer, mic_element_name);
    if (!elem) {
        snd_mixer_close(mixer);
        return -ENOENT;
    }

    if (!snd_mixer_selem_has_capture_volume(elem)) {
        snd_mixer_close(mixer);
        return -EINVAL;
    }

    err = snd_mixer_selem_get_capture_volume_range(elem, &minv, &maxv);
    if (err < 0) {
        snd_mixer_close(mixer);
        return err;
    }

    raw = percent_to_raw(minv, maxv, percent);

    err = snd_mixer_selem_set_capture_volume_all(elem, raw);

    if (err == 0) {
        printf("Set simple mixer mic element '%s' to %d%%, raw=%ld, range=[%ld..%ld]\n",
               mic_element_name,
               percent,
               raw,
               minv,
               maxv);
    }

    snd_mixer_close(mixer);
    return err;
}

/*
    Set raw ALSA integer control by exact name.

    This works when the control name is something like:

        "Mic Capture Volume"
        "Capture Volume"
*/
static int set_mic_volume_raw_control(
    int card,
    const char *control_name,
    int percent
    )
{
    char hw_name[32];
    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_list_t *list = NULL;
    int err;
    int count;

    snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

    err = snd_ctl_open(&ctl, hw_name, 0);
    if (err < 0)
        return err;

    err = snd_ctl_elem_list_malloc(&list);
    if (err < 0)
        goto done;

    err = snd_ctl_elem_list(ctl, list);
    if (err < 0)
        goto done;

    count = snd_ctl_elem_list_get_count(list);

    err = snd_ctl_elem_list_alloc_space(list, count);
    if (err < 0)
        goto done;

    err = snd_ctl_elem_list(ctl, list);
    if (err < 0)
        goto done;

    for (int i = 0; i < count; ++i) {
        snd_ctl_elem_id_t *id;
        snd_ctl_elem_info_t *info;
        snd_ctl_elem_value_t *value;
        const char *name;
        snd_ctl_elem_type_t type;
        unsigned int channels;
        long minv;
        long maxv;
        long raw;

        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_info_alloca(&info);
        snd_ctl_elem_value_alloca(&value);

        snd_ctl_elem_list_get_id(list, i, id);

        name = snd_ctl_elem_id_get_name(id);
        if (!name || strcmp(name, control_name) != 0)
            continue;

        snd_ctl_elem_info_set_id(info, id);

        err = snd_ctl_elem_info(ctl, info);
        if (err < 0)
            goto done;

        if (!snd_ctl_elem_info_is_writable(info)) {
            err = -EACCES;
            goto done;
        }

        type = snd_ctl_elem_info_get_type(info);
        if (type != SND_CTL_ELEM_TYPE_INTEGER) {
            err = -EINVAL;
            goto done;
        }

        minv = snd_ctl_elem_info_get_min(info);
        maxv = snd_ctl_elem_info_get_max(info);
        raw = percent_to_raw(minv, maxv, percent);

        channels = snd_ctl_elem_info_get_count(info);

        snd_ctl_elem_value_set_id(value, id);

        err = snd_ctl_elem_read(ctl, value);
        if (err < 0)
            goto done;

        for (unsigned int c = 0; c < channels; ++c)
            snd_ctl_elem_value_set_integer(value, c, raw);

        err = snd_ctl_elem_write(ctl, value);

        if (err == 0) {
            printf("Set raw mic control '%s' to %d%%, raw=%ld, range=[%ld..%ld]\n",
                   control_name,
                   percent,
                   raw,
                   minv,
                   maxv);
        }

        goto done;
    }

    err = -ENOENT;

done:
    if (list) {
        snd_ctl_elem_list_free_space(list);
        snd_ctl_elem_list_free(list);
    }

    if (ctl)
        snd_ctl_close(ctl);

    return err;
}

/*
    Disable AGC using simple mixer API.

    This works when AGC is exposed as a simple mixer switch.

    Example:

        "Auto Gain Control"
        "Mic Boost"
*/
static int disable_agc_simple(
    int card,
    const char *agc_element_name
    )
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *elem;
    int err;
    int changed = 0;

    err = open_simple_mixer(card, &mixer);
    if (err < 0)
        return err;

    elem = find_simple_element_exact(mixer, agc_element_name);
    if (!elem) {
        snd_mixer_close(mixer);
        return -ENOENT;
    }

    if (snd_mixer_selem_has_capture_switch(elem)) {
        err = snd_mixer_selem_set_capture_switch_all(elem, 0);
        if (err < 0) {
            snd_mixer_close(mixer);
            return err;
        }

        printf("Disabled simple capture switch '%s'\n", agc_element_name);
        changed = 1;
    }

    if (snd_mixer_selem_has_playback_switch(elem)) {
        err = snd_mixer_selem_set_playback_switch_all(elem, 0);
        if (err < 0) {
            snd_mixer_close(mixer);
            return err;
        }

        printf("Disabled simple playback switch '%s'\n", agc_element_name);
        changed = 1;
    }

    snd_mixer_close(mixer);

    return changed ? 0 : -EINVAL;
}

/*
    Disable AGC using raw ALSA control API.

    Supports:

        BOOLEAN controls:
            set to 0

        INTEGER controls:
            set to minimum value

        ENUMERATED controls:
            set to item 0

    Important:

        Since you said you have the exact element names, this function does
        not guess enum text such as "Off" or "Disabled".

        For enum controls, it sets index 0. On many devices, index 0 is Off.
        If your AGC enum uses a different Off index, change off_enum_index.
*/
static int disable_agc_raw_control(
    int card,
    const char *control_name
    )
{
    const unsigned int off_enum_index = 0;

    char hw_name[32];
    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_list_t *list = NULL;
    int err;
    int count;

    snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

    err = snd_ctl_open(&ctl, hw_name, 0);
    if (err < 0)
        return err;

    err = snd_ctl_elem_list_malloc(&list);
    if (err < 0)
        goto done;

    err = snd_ctl_elem_list(ctl, list);
    if (err < 0)
        goto done;

    count = snd_ctl_elem_list_get_count(list);

    err = snd_ctl_elem_list_alloc_space(list, count);
    if (err < 0)
        goto done;

    err = snd_ctl_elem_list(ctl, list);
    if (err < 0)
        goto done;

    for (int i = 0; i < count; ++i) {
        snd_ctl_elem_id_t *id;
        snd_ctl_elem_info_t *info;
        snd_ctl_elem_value_t *value;
        const char *name;
        snd_ctl_elem_type_t type;
        unsigned int channels;

        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_info_alloca(&info);
        snd_ctl_elem_value_alloca(&value);

        snd_ctl_elem_list_get_id(list, i, id);

        name = snd_ctl_elem_id_get_name(id);
        if (!name || strcmp(name, control_name) != 0)
            continue;

        snd_ctl_elem_info_set_id(info, id);

        err = snd_ctl_elem_info(ctl, info);
        if (err < 0)
            goto done;

        if (!snd_ctl_elem_info_is_writable(info)) {
            err = -EACCES;
            goto done;
        }

        type = snd_ctl_elem_info_get_type(info);
        channels = snd_ctl_elem_info_get_count(info);

        snd_ctl_elem_value_set_id(value, id);

        err = snd_ctl_elem_read(ctl, value);
        if (err < 0)
            goto done;

        if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
            for (unsigned int c = 0; c < channels; ++c)
                snd_ctl_elem_value_set_boolean(value, c, 0);
        }
        else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
            long minv = snd_ctl_elem_info_get_min(info);

            for (unsigned int c = 0; c < channels; ++c)
                snd_ctl_elem_value_set_integer(value, c, minv);
        }
        else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
            unsigned int item_count = snd_ctl_elem_info_get_items(info);

            if (off_enum_index >= item_count) {
                err = -EINVAL;
                goto done;
            }

            for (unsigned int c = 0; c < channels; ++c)
                snd_ctl_elem_value_set_enumerated(value, c, off_enum_index);
        }
        else {
            err = -EINVAL;
            goto done;
        }

        err = snd_ctl_elem_write(ctl, value);

        if (err == 0) {
            printf("Disabled raw AGC control '%s'\n", control_name);
        }

        goto done;
    }

    err = -ENOENT;

done:
    if (list) {
        snd_ctl_elem_list_free_space(list);
        snd_ctl_elem_list_free(list);
    }

    if (ctl)
        snd_ctl_close(ctl);

    return err;
}

//---------------------------------------------------------------//

static const char *elem_type_to_string(snd_ctl_elem_type_t type)
{
    switch (type)
    {
    case SND_CTL_ELEM_TYPE_NONE:
        return "NONE";


    case SND_CTL_ELEM_TYPE_BOOLEAN:
        return "BOOLEAN";


    case SND_CTL_ELEM_TYPE_INTEGER:
        return "INTEGER";


    case SND_CTL_ELEM_TYPE_ENUMERATED:
        return "ENUMERATED";


    case SND_CTL_ELEM_TYPE_BYTES:
        return "BYTES";


    case SND_CTL_ELEM_TYPE_IEC958:
        return "IEC958";


    case SND_CTL_ELEM_TYPE_INTEGER64:
        return "INTEGER64";


    default:
        return "UNKNOWN";
    }
}


static void print_element_info(snd_ctl_t *ctl, snd_ctl_elem_id_t *id)
{
    snd_ctl_elem_info_t *info;


    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_info_set_id(info, id);


    int err = snd_ctl_elem_info(ctl, info);
    if (err < 0)
    {
        printf(" Could not read element info: %s\n", snd_strerror(err));
        return;
    }


    const char *name = snd_ctl_elem_id_get_name(id);
    snd_ctl_elem_iface_t iface = snd_ctl_elem_id_get_interface(id);
    unsigned int index = snd_ctl_elem_id_get_index(id);
    unsigned int device = snd_ctl_elem_id_get_device(id);
    unsigned int subdevice = snd_ctl_elem_id_get_subdevice(id);


    snd_ctl_elem_type_t type = snd_ctl_elem_info_get_type(info);
    unsigned int count = snd_ctl_elem_info_get_count(info);


    printf(" Element:\n");
    printf(" Name : %s\n", name);
    printf(" Interface : %s\n", snd_ctl_elem_iface_name(iface));
    printf(" Type : %s\n", elem_type_to_string(type));
    printf(" Count : %u\n", count);
    printf(" Index : %u\n", index);
    printf(" Device : %u\n", device);
    printf(" Subdevice : %u\n", subdevice);


    if (type == SND_CTL_ELEM_TYPE_INTEGER)
    {
        long min = snd_ctl_elem_info_get_min(info);
        long max = snd_ctl_elem_info_get_max(info);
        long step = snd_ctl_elem_info_get_step(info);


        printf(" Min : %ld\n", min);
        printf(" Max : %ld\n", max);
        printf(" Step : %ld\n", step);
    }
    else if (type == SND_CTL_ELEM_TYPE_INTEGER64)
    {
        long long min = snd_ctl_elem_info_get_min64(info);
        long long max = snd_ctl_elem_info_get_max64(info);
        long long step = snd_ctl_elem_info_get_step64(info);


        printf(" Min : %lld\n", min);
        printf(" Max : %lld\n", max);
        printf(" Step : %lld\n", step);
    }
    else if (type == SND_CTL_ELEM_TYPE_ENUMERATED)
    {
        unsigned int items = snd_ctl_elem_info_get_items(info);


        printf(" Items : %u\n", items);


        for (unsigned int i = 0; i < items; ++i)
        {
            snd_ctl_elem_info_set_item(info, i);


            err = snd_ctl_elem_info(ctl, info);
            if (err >= 0)
            {
                printf(" [%u] %s\n",
                       i,
                       snd_ctl_elem_info_get_item_name(info));
            }
        }
    }


    printf("\n");
}


static void list_card_elements(int card_number)
{
    char hw_name[32];
    snprintf(hw_name, sizeof(hw_name), "hw:%d", card_number);


    snd_ctl_t *ctl = NULL;


    int err = snd_ctl_open(&ctl, hw_name, 0);
    if (err < 0)
    {
        printf("Could not open control device %s: %s\n",
               hw_name,
               snd_strerror(err));
        return;
    }


    snd_ctl_card_info_t *card_info;
    snd_ctl_card_info_alloca(&card_info);


    err = snd_ctl_card_info(ctl, card_info);
    if (err < 0)
    {
        printf("Could not read card info for %s: %s\n",
               hw_name,
               snd_strerror(err));
        snd_ctl_close(ctl);
        return;
    }


    printf("============================================================\n");
    printf("Card %d\n", card_number);
    printf(" ID : %s\n", snd_ctl_card_info_get_id(card_info));
    printf(" Driver : %s\n", snd_ctl_card_info_get_driver(card_info));
    printf(" Name : %s\n", snd_ctl_card_info_get_name(card_info));
    printf(" Long Name : %s\n", snd_ctl_card_info_get_longname(card_info));
    printf(" Mixer : %s\n", snd_ctl_card_info_get_mixername(card_info));
    printf(" Device : %s\n", hw_name);
    printf("\n");


    snd_ctl_elem_list_t *elem_list;
    snd_ctl_elem_list_alloca(&elem_list);


    /*
First call gets the number of elements.
*/
    err = snd_ctl_elem_list(ctl, elem_list);
    if (err < 0)
    {
        printf("Could not get element count: %s\n", snd_strerror(err));
        snd_ctl_close(ctl);
        return;
    }


    unsigned int elem_count = snd_ctl_elem_list_get_count(elem_list);


    /*
Allocate space for the element IDs, then call again to fill them.
*/
    snd_ctl_elem_list_alloc_space(elem_list, elem_count);


    err = snd_ctl_elem_list(ctl, elem_list);
    if (err < 0)
    {
        printf("Could not get element list: %s\n", snd_strerror(err));
        snd_ctl_elem_list_free_space(elem_list);
        snd_ctl_close(ctl);
        return;
    }


    unsigned int used = snd_ctl_elem_list_get_used(elem_list);


    printf("Control Elements: %u\n\n", used);


    for (unsigned int i = 0; i < used; ++i)
    {
        snd_ctl_elem_id_t *id;


        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_list_get_id(elem_list, i, id);


        print_element_info(ctl, id);
    }


    snd_ctl_elem_list_free_space(elem_list);
    snd_ctl_close(ctl);
}



void LinuxListSoundCardsAndElements(void)
{

    int card = -1;
    int err;


    /*
snd_card_next() iterates through ALSA card numbers.
Start with card = -1, then ALSA returns the first card.
*/
    err = snd_card_next(&card);
    if (err < 0)
    {
        fprintf(stderr, "Cannot get first ALSA card: %s\n", snd_strerror(err));
        return ;
    }


    if (card < 0)
    {
        printf("No ALSA sound cards found.\n");
        return ;
    }


    while (card >= 0)
    {
        list_card_elements(card);


        err = snd_card_next(&card);
        if (err < 0)
        {
            fprintf(stderr, "Cannot get next ALSA card: %s\n", snd_strerror(err));
            return ;
        }
    }

}
#endif