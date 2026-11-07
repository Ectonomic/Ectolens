/* SPDX-FileCopyrightText: 2018 Wim Taymans (PipeWire's src/examples/video-src.c, which this adapts)
 * SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: MIT
 *
 * A stand-in for KWin's screen cast: a PipeWire video source that offers a
 * variable frame rate with a ceiling, stamps each frame with CLOCK_MONOTONIC
 * as KWin does, marks damage, and draws screen-like moving content.
 * Prints its node id on the first line, then runs until killed.
 * usage: ectolens-screen-source WIDTH HEIGHT FPS
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#define BPP 4

struct data {
    struct pw_main_loop *loop;
    struct spa_source *timer;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info_raw format;
    int32_t stride;
    uint32_t seq;
    uint32_t width, height, fps;
    int printed;
};

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * SPA_NSEC_PER_SEC + ts.tv_nsec;
}

static void on_process(void *userdata)
{
    struct data *d = userdata;
    struct pw_buffer *b = pw_stream_dequeue_buffer(d->stream);
    if (!b)
        return;
    struct spa_buffer *buf = b->buffer;
    uint8_t *p = buf->datas[0].data;
    if (!p)
        return;

    struct spa_meta_header *h = spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*h));
    if (h) {
        h->pts = (int64_t)monotonic_ns();
        h->flags = 0;
        h->seq = d->seq;
        h->dts_offset = 0;
    }
    struct spa_meta *m = spa_buffer_find_meta(buf, SPA_META_VideoDamage);
    if (m) {
        struct spa_meta_region *r = spa_meta_first(m);
        if (spa_meta_check(r, m)) {
            r->region.position = SPA_POINT(0, 0);
            r->region.size = d->format.size;
            r++;
        }
        if (spa_meta_check(r, m))
            r->region = SPA_REGION(0, 0, 0, 0);
    }

    /* Screen-like content: text-like rows that scroll, and a box that moves. */
    const uint32_t w = d->format.size.width, hgt = d->format.size.height;
    const uint32_t scroll = d->seq * 3, boxX = (d->seq * 7) % (w > 120 ? w - 120 : 1);
    for (uint32_t y = 0; y < hgt; y++) {
        uint32_t *row = (uint32_t *)(p + (size_t)y * d->stride);
        const uint32_t line = (y + scroll) / 18, inLine = (y + scroll) % 18;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = 0x00f4f4f4;
            if (inLine > 4 && inLine < 13 && x > 40 && x < 40 + ((line * 97) % (w - 80)) && ((x / 7 + line) % 5) != 0)
                c = (line % 3 == 0) ? 0x00b83607 : 0x00303030;
            if (x >= boxX && x < boxX + 120 && y >= 300 % hgt && y < (300 % hgt) + 80)
                c = 0x00ff4500;
            row[x] = c;
        }
    }
    d->seq++;

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->size = hgt * d->stride;
    buf->datas[0].chunk->stride = d->stride;
    pw_stream_queue_buffer(d->stream, b);
}

static void on_timeout(void *userdata, uint64_t expirations)
{
    struct data *d = userdata;
    pw_stream_trigger_process(d->stream);
}

static void on_state_changed(void *userdata, enum pw_stream_state old, enum pw_stream_state state, const char *error)
{
    struct data *d = userdata;
    if (state == PW_STREAM_STATE_ERROR) {
        fprintf(stderr, "stream error: %s\n", error ? error : "");
        pw_main_loop_quit(d->loop);
    } else if (state == PW_STREAM_STATE_PAUSED) {
        if (!d->printed) {
            printf("%u\n", pw_stream_get_node_id(d->stream));
            fflush(stdout);
            d->printed = 1;
        }
        pw_loop_update_timer(pw_main_loop_get_loop(d->loop), d->timer, NULL, NULL, false);
    } else if (state == PW_STREAM_STATE_STREAMING) {
        struct timespec timeout = {0, 1}, interval = {0, (long)(SPA_NSEC_PER_SEC / d->fps)};
        pw_loop_update_timer(pw_main_loop_get_loop(d->loop), d->timer, &timeout, &interval, false);
    }
}

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
    struct data *d = userdata;
    if (param == NULL || id != SPA_PARAM_Format)
        return;
    spa_format_video_raw_parse(param, &d->format);
    d->stride = SPA_ROUND_UP_N(d->format.size.width * BPP, 4);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[3];
    params[0] = spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, 32),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size, SPA_POD_Int(d->stride * d->format.size.height),
        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(d->stride),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
    params[1] = spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));
    params[2] = spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage),
        SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * 16, sizeof(struct spa_meta_region), sizeof(struct spa_meta_region) * 16));
    pw_stream_update_params(d->stream, params, 3);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
};

static void do_quit(void *userdata, int signal_number)
{
    pw_main_loop_quit(((struct data *)userdata)->loop);
}

int main(int argc, char *argv[])
{
    struct data d = {0};
    d.width = argc > 1 ? atoi(argv[1]) : 1280;
    d.height = argc > 2 ? atoi(argv[2]) : 720;
    d.fps = argc > 3 ? atoi(argv[3]) : 60;

    pw_init(&argc, &argv);
    d.loop = pw_main_loop_new(NULL);
    pw_loop_add_signal(pw_main_loop_get_loop(d.loop), SIGINT, do_quit, &d);
    pw_loop_add_signal(pw_main_loop_get_loop(d.loop), SIGTERM, do_quit, &d);
    d.context = pw_context_new(pw_main_loop_get_loop(d.loop), NULL, 0);
    d.timer = pw_loop_add_timer(pw_main_loop_get_loop(d.loop), on_timeout, &d);
    d.core = pw_context_connect(d.context, NULL, 0);
    if (!d.core) {
        fprintf(stderr, "can't connect: %m\n");
        return 1;
    }
    d.stream = pw_stream_new(d.core, "ectolens-screen-source",
        pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", PW_KEY_NODE_NAME, "ectolens-screen-source", NULL));

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    /* As KWin offers a screen: a variable rate (0/1) with a ceiling. */
    params[0] = spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA),
        SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&SPA_RECTANGLE(d.width, d.height)),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)),
        SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(d.fps, 1), &SPA_FRACTION(1, 1), &SPA_FRACTION(d.fps, 1)));

    pw_stream_add_listener(d.stream, &d.stream_listener, &stream_events, &d);
    pw_stream_connect(d.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);
    pw_main_loop_run(d.loop);

    pw_stream_destroy(d.stream);
    pw_context_destroy(d.context);
    pw_main_loop_destroy(d.loop);
    pw_deinit();
    return 0;
}
