@echo off
cd /d %~dp0
ntos_windows.exe fingerkb.dll shellchrome.dll cortana.dll --fs fs --record session.script
echo session.script + ntos-win.log + shot-*.png record what you did
