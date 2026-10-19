@echo off
rem Launcher for OpenGothic with Elden Ring style combat (Gothic II: Night of the Raven).
rem Usable as Steam launch option for Gothic II:  "D:\OpenGothic-EldenRing\play-elden.bat" %command%
rem The original command passed by Steam (%*) is ignored. "start /wait" keeps this script
rem alive until the game exits, so Steam keeps counting Gothic II playtime.
rem Without -eldencombat the game uses vanilla Gothic combat; F9 toggles it in game.
rem OPENGOTHIC_ELDEN_ANIMS: Elden Ring animations baked from your own game copy (stays local).
set GAME=C:\Program Files (x86)\Steam\steamapps\common\Gothic II
set OPENGOTHIC_ELDEN_ANIMS=D:\OpenMW-EldenRing\ELDEN-RING-Combat-Rewrite\assets\player_anims.bin
set EXE=%~dp0build\opengothic\Gothic2Notr.exe
cd /d "%~dp0build\opengothic"
start "" /wait "%EXE%" -g "%GAME%" -eldencombat
