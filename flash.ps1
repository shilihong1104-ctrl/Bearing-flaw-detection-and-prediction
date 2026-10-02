# Flash blink_demo.elf to NUCLEO-U575ZI-Q via OpenOCD
# Usage: .\flash.ps1
# Prereq: board connected via USB, ST-Link driver installed

$openocd   = "D:\DevEnv\DevEnv\DevEnv\openocd-v0.12.0-i686-w64-mingw32\bin\openocd.exe"
$elf       = "D:\STM32\blink_demo\cmake-build-debug\blink_demo.elf"
$interface = "interface/stlink.cfg"
$target    = "target/stm32u5x.cfg"

Write-Host "=== Flash blink_demo.elf -> NUCLEO-U575ZI-Q ===" -ForegroundColor Cyan
Write-Host ""

# 1. Check elf exists
if (-not (Test-Path $elf)) {
    Write-Host "[FAIL] elf not found: $elf" -ForegroundColor Red
    Write-Host "       Build in CLion first (Ctrl+F9)" -ForegroundColor Yellow
    exit 1
}
Write-Host "[OK] elf found ($([math]::Round((Get-Item $elf).Length/1KB, 1)) KB)" -ForegroundColor Green

# 2. Kill leftover OpenOCD
taskkill /F /IM openocd.exe 2>$null | Out-Null

# 3. Flash
Write-Host "`n[..] Flashing..." -ForegroundColor Cyan
& $openocd -f $interface -f $target -c "program $elf verify reset exit"

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[OK] Flash success! Board reset." -ForegroundColor Green
} else {
    Write-Host "`n[FAIL] Flash failed" -ForegroundColor Red
    Write-Host "       Check: 1) USB cable 2) ST-Link driver 3) power LED" -ForegroundColor Yellow
}