@echo off
rem Runs the online relay server on this PC, on TCP port 51020 (or the port given after the file name).
rem Leave this window open while people play. See SERVER-SETUP.txt for the firewall and router steps.
title Mario Party 9 online relay
cd /d "%~dp0"
set PORT=%1
if "%PORT%"=="" set PORT=51020
wsr_relay.exe %PORT%
pause
