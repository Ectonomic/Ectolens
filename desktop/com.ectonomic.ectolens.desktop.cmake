[Desktop Entry]
GenericName=Screenshot and Screen Recorder
Name=Ectolens
Comment=Take screenshots and screen recordings
Categories=Qt;KDE;Utility;
Keywords=snapshot;capture;print;screenshot;snipping;snipping tool;snip;
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens
Icon=ectolens
Type=Application
StartupNotify=false
Actions=FullScreenScreenShot;CurrentMonitorScreenShot;ActiveWindowScreenShot;RectangularRegionScreenShot;WindowUnderCursorScreenShot;RecordRegion;RecordScreen;RecordWindow;OpenWithoutScreenshot;
DBusActivatable=true
X-DBUS-StartupType=Unique
# KWin 6.7 lets only apps that declare these use its screenshot and screen-recording interfaces
# (Spectacle 6.7.5 carries the same lines; its master dropped them for newer KWin).
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,zkde_screencast_unstable_v1
X-DBUS-ServiceName=com.ectonomic.Ectolens
X-KDE-Shortcuts=Print,Meta+Shift+S

[Desktop Action FullScreenScreenShot]
Name=Capture Entire Desktop
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -f
X-KDE-Shortcuts=Shift+Print

[Desktop Action CurrentMonitorScreenShot]
Name=Capture Current Monitor
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -m


[Desktop Action ActiveWindowScreenShot]
Name=Capture Active Window
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -a
X-KDE-Shortcuts=Meta+Print

[Desktop Action RectangularRegionScreenShot]
Name=Capture Rectangular Region
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -r
X-KDE-Shortcuts=Meta+Shift+Print

[Desktop Action WindowUnderCursorScreenShot]
Name=Capture Window Under Cursor
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -u
X-KDE-Shortcuts=Meta+Ctrl+Print

[Desktop Action RecordRegion]
Name=Start/Stop Region Recording
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -R region
X-KDE-Shortcuts=Meta+Shift+R,Meta+R

[Desktop Action RecordScreen]
Name=Start/Stop Screen Recording
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -R screen
X-KDE-Shortcuts=Meta+Alt+R

[Desktop Action RecordWindow]
Name=Start/Stop Window Recording
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -R window
X-KDE-Shortcuts=Meta+Ctrl+R

[Desktop Action OpenWithoutScreenshot]
Name=Launch without taking a screenshot
Exec=${KDE_INSTALL_FULL_BINDIR}/ectolens -l
