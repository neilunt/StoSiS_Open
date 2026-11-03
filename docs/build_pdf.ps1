# Erzeugt PDFs aus den HTML-Dokumenten in docs mit Microsoft Edge (ohne Fenster).
# Aufruf in PowerShell im Ordner docs:
#   .\build_pdf.ps1                            alle vorhandenen Dokumente
#   .\build_pdf.ps1 StoSiS_Methodenhandbuch    nur dieses Dokument
param([string[]]$Names = @("StoSiS_Methodenhandbuch", "StoSiS_Arbeitsdokument"))

$ErrorActionPreference = "Stop"
$dir  = Split-Path -Parent $MyInvocation.MyCommand.Path

$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edge)) { $edge = "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe" }

# Separates, leeres Profil: so stoert der Druck kein geoeffnetes Edge-Fenster.
$profileDir = Join-Path $env:TEMP "stosis_pdf_profile"

foreach ($name in $Names) {
    $html = Join-Path $dir "$name.html"
    $pdf  = Join-Path $dir "$name.pdf"
    if (-not (Test-Path $html)) { "Uebersprungen (nicht vorhanden): $html"; continue }
    $url = "file:///" + ($html -replace '\\', '/')

    if (Test-Path $pdf) { Remove-Item $pdf }
    $arguments = @(
        "--headless=new", "--disable-gpu", "--no-first-run",
        "--user-data-dir=`"$profileDir`"",
        "--no-pdf-header-footer",
        "--virtual-time-budget=20000",     # Zeit zum Laden aller Abbildungen und fuer das Inhaltsverzeichnis-Skript
        "--print-to-pdf=`"$pdf`"",
        "`"$url`""
    )
    $process = Start-Process -FilePath $edge -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if (Test-Path $pdf) { "Erzeugt: $pdf (" + [math]::Round((Get-Item $pdf).Length / 1KB) + " KB)" }
    else { throw "PDF wurde nicht erzeugt (Edge-Exitcode $($process.ExitCode))." }
}
