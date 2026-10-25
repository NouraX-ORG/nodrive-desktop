#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimal org.freedesktop.portal.Settings service for the Linux smoke test.

Desktops tell Qt apps about light or dark mode through the XDG desktop portal
(namespace "org.freedesktop.appearance", key "color-scheme": 1 = prefer dark,
2 = prefer light). Qt 6.11's GNOME theme reads it with ReadAll on
org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop
(qtbase src/gui/platform/unix/qgnomeportalinterface.cpp). This service answers
exactly that, so the CI run can show the app as a user with dark mode on sees it.

Usage: fake-portal.py light|dark    (runs on the session bus until killed)
Needs python3-dbus and python3-gi (GLib main loop).
"""

import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

BUS_NAME = "org.freedesktop.portal.Desktop"
OBJECT_PATH = "/org/freedesktop/portal/desktop"
IFACE = "org.freedesktop.portal.Settings"
APPEARANCE = "org.freedesktop.appearance"


class NotFound(dbus.exceptions.DBusException):
    _dbus_error_name = "org.freedesktop.portal.Error.NotFound"


class Settings(dbus.service.Object):
    def __init__(self, bus, scheme: int):
        super().__init__(bus, OBJECT_PATH)
        self.values = {APPEARANCE: {"color-scheme": dbus.UInt32(scheme), "contrast": dbus.UInt32(0)}}

    def _lookup(self, namespace, key):
        try:
            return self.values[str(namespace)][str(key)]
        except KeyError:
            raise NotFound(f"{namespace} {key} not found")

    @dbus.service.method(IFACE, in_signature="as", out_signature="a{sa{sv}}")
    def ReadAll(self, namespaces):
        wanted = [str(n) for n in namespaces]
        out = dbus.Dictionary({}, signature="sa{sv}")
        for ns, values in self.values.items():
            if not wanted or any(ns == w or (w.endswith("*") and ns.startswith(w[:-1])) for w in wanted):
                out[ns] = dbus.Dictionary(values, signature="sv")
        return out

    @dbus.service.method(IFACE, in_signature="ss", out_signature="v")
    def ReadOne(self, namespace, key):
        return self._lookup(namespace, key)

    # Deprecated portal method: the value is wrapped in one more variant.
    @dbus.service.method(IFACE, in_signature="ss", out_signature="v")
    def Read(self, namespace, key):
        value = self._lookup(namespace, key)
        return dbus.UInt32(value, variant_level=1)

    @dbus.service.signal(IFACE, signature="ssv")
    def SettingChanged(self, namespace, key, value):
        pass

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
    def Get(self, interface, prop):
        if interface == IFACE and prop == "version":
            return dbus.UInt32(2)
        raise dbus.exceptions.DBusException("org.freedesktop.DBus.Error.UnknownProperty", prop)

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"version": dbus.UInt32(2)} if interface == IFACE else {}


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("light", "dark"):
        print(__doc__, file=sys.stderr)
        return 2
    scheme = 1 if sys.argv[1] == "dark" else 2
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName(BUS_NAME, bus, do_not_queue=True)
    Settings(bus, scheme)
    print(f"fake-portal: {BUS_NAME} color-scheme={scheme} ({sys.argv[1]})", flush=True)
    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    del name
    return 0


if __name__ == "__main__":
    sys.exit(main())
