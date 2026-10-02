# Simple serial port monitor
# Usage: .\serial_monitor.ps1 -Port COM3 -Baud 115200
# Press Ctrl+C to exit

param(
    [string]$Port = "COM3",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

try {
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
    $sp.ReadTimeout = 500
    $sp.Open()
    Write-Host "Connected $Port @ ${Baud}bps, Ctrl+C to exit" -ForegroundColor Green

    while ($true) {
        try {
            $line = $sp.ReadLine()
            Write-Host $line
        } catch [TimeoutException] {
            # timeout, continue
        }
    }
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    Write-Host "`nSerial port closed" -ForegroundColor Yellow
}