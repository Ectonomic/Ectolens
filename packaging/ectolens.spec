# Ectolens for Fedora, built in COPR from the repository (.copr/Makefile makes the source
# archive, KPipeWire submodule included). It installs beside Spectacle and replaces none
# of its files: Ectolens's KPipeWire is private to it, in %%{_libdir}/ectolens, and the files
# Spectacle's own package carries (its handbook, man pages, translations under its name,
# settings schema, D-Bus interface, logging categories) stay Spectacle's.
# After installing, each person moves Spectacle's shortcuts with `ectolens-shortcuts take`
# (and `ectolens-shortcuts give-back` before removing the package).

# the private KPipeWire neither satisfies nor needs the system's
%global __provides_exclude_from ^%{_libdir}/%{name}/.*$
%global __requires_exclude ^libKPipeWire[A-Za-z]*\\.so.*$
%{!?snapinfo: %global snapinfo 0}

Name:           ectolens
Version:        6.8.80
Release:        0.%{snapinfo}%{?dist}
Summary:        Screenshots and screen recordings for KDE Plasma
License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND (LGPL-2.1-only OR LGPL-3.0-only) AND LGPL-2.1-or-later AND OFL-1.1
URL:            https://github.com/Ectonomic/Ectolens
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake ninja-build gcc-c++ gettext extra-cmake-modules
BuildRequires:  cmake(Qt6Core) cmake(Qt6GuiPrivate) cmake(Qt6Quick) cmake(Qt6QuickControls2) cmake(Qt6WaylandClient)
BuildRequires:  cmake(Qt6Multimedia) cmake(Qt6Concurrent) cmake(Qt6PrintSupport) cmake(Qt6Test) qt6-qtshadertools
BuildRequires:  cmake(KF6CoreAddons) cmake(KF6WidgetsAddons) cmake(KF6DBusAddons) cmake(KF6Notifications) cmake(KF6Config)
BuildRequires:  cmake(KF6I18n) cmake(KF6KIO) cmake(KF6WindowSystem) cmake(KF6GlobalAccel) cmake(KF6XmlGui)
BuildRequires:  cmake(KF6GuiAddons) cmake(KF6KirigamiPlatform) cmake(KF6StatusNotifierItem) cmake(KF6Prison)
BuildRequires:  cmake(KF6Crash) cmake(KF6Purpose) cmake(LayerShellQt) cmake(PlasmaWaylandProtocols)
BuildRequires:  pkgconfig(wayland-client) pkgconfig(libpipewire-0.3) pkgconfig(libavcodec) pkgconfig(libavutil)
BuildRequires:  pkgconfig(libavformat) pkgconfig(libavfilter) pkgconfig(libswscale) pkgconfig(gbm) pkgconfig(epoxy)
BuildRequires:  pkgconfig(libdrm) pkgconfig(libva) pkgconfig(libva-drm) pkgconfig(egl) pkgconfig(tesseract)
BuildRequires:  pkgconfig(xcb-xfixes) pkgconfig(xcb-image) pkgconfig(xcb-util) pkgconfig(xcb-cursor) pkgconfig(xcb-randr)

# QML modules and tools loaded at run time, which no library dependency brings
Requires:       kf6-kirigami qt6-qtdeclarative qt6-qtmultimedia qt6-qtimageformats
Requires:       tesseract-langpack-eng
# ectolens-shortcuts talks to KDE's shortcut service with gdbus
Requires:       glib2

%description
Ectolens is a fork of Spectacle, KDE Plasma's screenshot tool. Print Screen opens
one overlay for pictures and video: point at a window, click the desktop or drag
an area, then Capture or Record. It records with a choice of quality, encoder and
frame rate, into MP4 files that survive a crash, and marks up, blurs private
information and shares captures. It installs beside Spectacle.

%prep
%autosetup -p1

%build
export CFLAGS="%{optflags}" CXXFLAGS="%{optflags}" LDFLAGS="%{?build_ldflags}"
# Ectolens's own KPipeWire, staged so Ectolens builds against it and not the system's
cmake -S 3rdparty/kpipewire -B kpw -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=%{_libdir}/%{name} -DKDE_INSTALL_LIBDIR=lib
cmake --build kpw %{?_smp_mflags}
DESTDIR=$PWD/stage cmake --install kpw
cmake -S . -B ecto -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} -DKDE_INSTALL_SYSTEMDUSERUNITDIR=%{_userunitdir} \
      -DCMAKE_PREFIX_PATH=$PWD/stage%{_libdir}/%{name} -DKPipeWire_DIR=$PWD/stage%{_libdir}/%{name}/lib/cmake/KPipeWire \
      -DCMAKE_INSTALL_RPATH=%{_libdir}/%{name}/lib
cmake --build ecto %{?_smp_mflags}

%install
DESTDIR=%{buildroot} cmake --install kpw
DESTDIR=%{buildroot} cmake --install ecto
# KPipeWire is Ectolens's alone: its shared libraries and nothing else
find %{buildroot}%{_libdir}/%{name} -mindepth 1 -maxdepth 1 ! -name lib -exec rm -r -- {} +
find %{buildroot}%{_libdir}/%{name}/lib -mindepth 1 -maxdepth 1 ! -name 'libKPipeWire*.so.*' -exec rm -r -- {} +
# Spectacle's own files, which Spectacle's package carries under the same names
for d in %{_datadir}/locale %{_datadir}/doc/HTML %{_mandir} %{_datadir}/config.kcfg \
         %{_datadir}/dbus-1/interfaces %{_datadir}/qlogging-categories6; do
    if [ -e "%{buildroot}$d" ]; then rm -r -- "%{buildroot}$d"; fi
done
if [ -e %{buildroot}%{_datadir}/icons/hicolor/icon-theme.cache ]; then rm -- %{buildroot}%{_datadir}/icons/hicolor/icon-theme.cache; fi
# the shortcut mover, the same code install.sh uses
install -Dm755 tools/install/ectolens-shortcuts %{buildroot}%{_bindir}/ectolens-shortcuts
install -Dm644 -t %{buildroot}%{_datadir}/%{name}/install tools/install/common.sh tools/install/shortcuts.sh

%files
%license LICENSES/* src/fonts/OFL.txt
%doc README.md
%{_bindir}/ectolens
%{_bindir}/ectolens-mcp
%{_bindir}/ectolens-shortcuts
%{_libdir}/%{name}/
%{_datadir}/%{name}/
%{_datadir}/applications/com.ectonomic.ectolens.desktop
%{_datadir}/dbus-1/services/com.ectonomic.ectolens.service
%{_datadir}/dbus-1/services/com.ectonomic.Ectolens.service
%{_datadir}/icons/hicolor/*/apps/ectolens.svg
%{_datadir}/kglobalaccel/com.ectonomic.ectolens.desktop
%{_datadir}/knotifications6/ectolens.notifyrc
%{_datadir}/metainfo/com.ectonomic.ectolens.appdata.xml
%{_userunitdir}/app-com.ectonomic.ectolens.service

%changelog
* Thu Nov 05 2026 Ectolens contributors - 6.8.80-0
- First package: snapshots of the ectolens branch, built in COPR
