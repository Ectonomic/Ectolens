/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: MIT
 *
 * Real input for the off-screen overlay test, through KWin's fake-input protocol
 * (granted by the desktop file run.sh installs in the sandbox). Commands on stdin,
 * one per line: "move X Y", "down", "up", "click", "key CODE" (Linux key code, pressed
 * and let go), "press CODE" and "release CODE" (a key held through what follows, as Shift
 * or Ctrl is held through a drag), "sleep MS". Prints "ok" after each.
 */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "fake-input-client-protocol.h"

static struct org_kde_kwin_fake_input *fake;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (strcmp(interface, org_kde_kwin_fake_input_interface.name) == 0) {
        fake = wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, version < 4 ? version : 4);
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}
static const struct wl_registry_listener listener = {global, global_remove};

static void nap(int ms)
{
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "no wayland display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL);
    wl_display_roundtrip(display);
    if (!fake) {
        fprintf(stderr, "fake input not granted\n");
        return 1;
    }
    org_kde_kwin_fake_input_authenticate(fake, "Ectolens test", "Drives the overlay in an off-screen test");
    wl_display_roundtrip(display);

    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        double x, y;
        int n;
        if (sscanf(line, "move %lf %lf", &x, &y) == 2) {
            org_kde_kwin_fake_input_pointer_motion_absolute(fake, wl_fixed_from_double(x), wl_fixed_from_double(y));
        } else if (strncmp(line, "down", 4) == 0) {
            org_kde_kwin_fake_input_button(fake, BTN_LEFT, 1);
        } else if (strncmp(line, "up", 2) == 0) {
            org_kde_kwin_fake_input_button(fake, BTN_LEFT, 0);
        } else if (strncmp(line, "click", 5) == 0) {
            org_kde_kwin_fake_input_button(fake, BTN_LEFT, 1);
            wl_display_roundtrip(display);
            nap(60);
            org_kde_kwin_fake_input_button(fake, BTN_LEFT, 0);
        } else if (sscanf(line, "key %d", &n) == 1) {
            org_kde_kwin_fake_input_keyboard_key(fake, n, 1);
            wl_display_roundtrip(display);
            nap(40);
            org_kde_kwin_fake_input_keyboard_key(fake, n, 0);
        } else if (sscanf(line, "press %d", &n) == 1) {
            org_kde_kwin_fake_input_keyboard_key(fake, n, 1);
        } else if (sscanf(line, "release %d", &n) == 1) {
            org_kde_kwin_fake_input_keyboard_key(fake, n, 0);
        } else if (sscanf(line, "sleep %d", &n) == 1) {
            nap(n);
        }
        wl_display_roundtrip(display);
        printf("ok\n");
        fflush(stdout);
    }
    wl_display_disconnect(display);
    return 0;
}
