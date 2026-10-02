@echo off
setlocal
set PORT=%~1
if "%PORT%"=="" set PORT=COM3

echo Gravando firmware CYD 2.8" em %PORT% ...
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 "%~dp0bootloader.bin" 0x8000 "%~dp0partition-table.bin" 0x10000 "%~dp0esp32.bin"
if errorlevel 1 (
  echo.
  echo Falha. Confira a porta ^(ex.: flash.bat COM4^) e o cabo USB de dados.
  exit /b 1
)
echo.
echo Pronto. Abra o monitor: idf.py -p %PORT% monitor
echo Portal: Wi-Fi ESP32-Setup / esp32setup - http://192.168.4.1
endlocal
