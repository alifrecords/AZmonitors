@echo off
rem AZ Monitoring — Azimxodjayev Music
rem VST3 ni tizim papkasiga o'rnatadi (Cubase, Ableton, FL Studio, Studio One, Reaper)
rem AAX bo'lsa — Pro Tools papkasiga ham.
rem Ishga tushirish: o'ng tugma -> "Run as administrator"

set VST3DIR=%CommonProgramFiles%\VST3
set AAXDIR=%CommonProgramFiles%\Avid\Audio\Plug-Ins

echo AZ Monitoring o'rnatilmoqda...
if not exist "%VST3DIR%" mkdir "%VST3DIR%"
xcopy /E /I /Y "%~dp0AZ Monitoring.vst3" "%VST3DIR%\AZ Monitoring.vst3" >nul || goto :err
echo   VST3 -> %VST3DIR%

if exist "%~dp0AZ Monitoring.aaxplugin" (
  if not exist "%AAXDIR%" mkdir "%AAXDIR%"
  xcopy /E /I /Y "%~dp0AZ Monitoring.aaxplugin" "%AAXDIR%\AZ Monitoring.aaxplugin" >nul || goto :err
  echo   AAX  -> %AAXDIR%
)

echo.
echo Tayyor. DAW'ni qayta ishga tushiring va plaginlarni qayta skanerlang.
pause
exit /b 0

:err
echo Xato: administrator sifatida ishga tushiring.
pause
exit /b 1
