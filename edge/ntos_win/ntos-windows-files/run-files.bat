@echo off
cd /d %~dp0
ntos_windows.exe html.dll fingerkb.dll shellchrome.dll chakra.dll edge.dll files.dll --fs fs --record session.script
echo session.script + ntos-win.log + shot-*.png record what you did
