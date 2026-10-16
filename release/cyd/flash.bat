@echo off
setlocal
set PORT=%~1
if "%PORT%"=="" set PORT=COM3

echo Gravando firmware CYD 2.8" em %PORT% (ota_0 @ 0x20000) ...
echo Esta gravacao nao apaga a NVS (Wi-Fi, token e ajustes da tela).
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 "%~dp0bootloader.bin" 0x8000 "%~dp0partition-table.bin" 0xf000 "%~dp0otadata.bin" 0x20000 "%~dp0esp32.bin"
if errorlevel 1 (
  echo.
  echo Falha. Confira a porta ^(ex.: flash.bat COM4^) e o cabo USB de dados.
  exit /b 1
)
echo.
echo Pronto. A NVS foi preservada. Monitor: idf.py -p %PORT% monitor
echo Portal: Wi-Fi ESP32-Setup / esp32setup - http://192.168.4.1
echo Proximas atualizacoes: POST /ota com esp32-ota.bin ^(sem cabo^).
endlocal
