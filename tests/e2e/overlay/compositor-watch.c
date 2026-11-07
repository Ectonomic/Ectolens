/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * How long the compositor takes to answer, measured from outside it (Print Screen
 * must not freeze the whole desktop while it loads). Every 5 ms it asks the compositor
 * for a wl_display.sync and times the answer. The compositor answers from the same main
 * thread that handles input and draws, so a slow answer is a frozen desktop. It takes no
 * picture and opens no window.
 *
 * usage: ectolens-compositor-watch [SECONDS] [THRESHOLD_MS]
 *   prints "STALL <wall clock> <ms>" for each answer slower than THRESHOLD_MS (default 50),
 *   and at the end (or on SIGINT/SIGTERM) "WATCH samples=N longest=MS stalls=N".
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static volatile sig_atomic_t stop = 0;
static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static double mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void wall(char *out, size_t n)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    struct tm tm;
    localtime_r(&t.tv_sec, &tm);
    size_t len = strftime(out, n, "%H:%M:%S", &tm);
    snprintf(out + len, n - len, ".%03ld", t.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    const double seconds = argc > 1 ? atof(argv[1]) : 60;
    const double threshold = argc > 2 ? atof(argv[2]) : 50;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "compositor-watch: no Wayland display\n");
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);

    const double end = mono_ms() + seconds * 1000.0;
    long samples = 0, stalls = 0;
    double longest = 0;
    while (!stop && mono_ms() < end) {
        const double t0 = mono_ms();
        if (wl_display_roundtrip(display) < 0) {
            fprintf(stderr, "compositor-watch: the connection closed\n");
            break;
        }
        const double ms = mono_ms() - t0;
        ++samples;
        if (ms > longest) {
            longest = ms;
        }
        if (ms > threshold) {
            ++stalls;
            char when[32];
            wall(when, sizeof when);
            printf("STALL %s %.0f\n", when, ms);
        }
        usleep(5000);
    }
    printf("WATCH samples=%ld longest=%.0f stalls=%ld\n", samples, longest, stalls);
    wl_display_disconnect(display);
    return 0;
}
