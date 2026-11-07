# A stand-in for Plasma's notification server on the test's private D-Bus: it records each
# notification (app, title, text, buttons) as a JSON line, so a check can read what Ectolens
# said after a capture. Nothing is shown. usage: python3 -I notify-server.py OUT.jsonl
import json, sys
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib

OUT = sys.argv[1]
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
name = dbus.service.BusName('org.freedesktop.Notifications', bus)


class Server(dbus.service.Object):
    count = 0

    @dbus.service.method('org.freedesktop.Notifications', in_signature='susssasa{sv}i', out_signature='u')
    def Notify(self, app, replaces, icon, summary, body, actions, hints, timeout):
        Server.count += 1
        # actions come as id, label pairs
        labels = [str(actions[i + 1]) for i in range(0, len(actions) - 1, 2)]
        urls = [str(u) for u in hints.get('x-kde-urls', [])]
        with open(OUT, 'a') as f:
            f.write(json.dumps({'app': str(app), 'title': str(summary), 'text': str(body), 'buttons': labels, 'urls': urls}) + '\n')
        return dbus.UInt32(Server.count)

    @dbus.service.method('org.freedesktop.Notifications', in_signature='', out_signature='as')
    def GetCapabilities(self):
        return ['actions', 'body', 'body-markup', 'x-kde-urls', 'inline-reply']

    @dbus.service.method('org.freedesktop.Notifications', in_signature='u', out_signature='')
    def CloseNotification(self, nid):
        pass

    @dbus.service.method('org.freedesktop.Notifications', in_signature='', out_signature='ssss')
    def GetServerInformation(self):
        return ('ectolens-test', 'Ectolens', '1', '1.2')


Server(bus, '/org/freedesktop/Notifications')
print('ready', flush=True)
GLib.MainLoop().run()
