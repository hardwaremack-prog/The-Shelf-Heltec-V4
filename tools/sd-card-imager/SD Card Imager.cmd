@echo off
rem Starts SD Card Imager (Windows asks to allow admin rights: reading a whole card needs them).
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0SD-Card-Imager.ps1"
