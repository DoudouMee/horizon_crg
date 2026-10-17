@echo off
cd /d "%~dp0"
set "GDK_PIXBUF_MODULE_FILE=%CD%\lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "HORIZON_POOL=%CD%\examples\demo\pool"
bin\horizon-crg-native.exe examples\demo\blocks.json "%HORIZON_POOL%" validation
if errorlevel 1 (
  echo Native project preparation failed. See the messages above.
  pause
  exit /b 1
)
start "" bin\horizon-imp.exe -c examples\demo\blocks.json examples\demo\pictures
