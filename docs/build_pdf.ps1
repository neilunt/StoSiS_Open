# Erzeugt StoSiS_Methoden.pdf aus StoSiS_Methoden.html mit Microsoft Edge
# (ohne Fenster). Aufruf in PowerShell im Ordner docs:   .\build_pdf.ps1

$ErrorActionPreference = "Stop"
$dir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$html = Join-Path $dir "StoSiS_Methoden.html"
$pdf  = Join-Path $dir "StoSiS_Methoden.pdf"

$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edge)) { $edge = "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe" }

# Eigenes, leeres Profil: so stoert der Druck kein geoeffnetes Edge-Fenster.
$profileDir = Join-Path $env:TEMP "stosis_pdf_profile"
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
