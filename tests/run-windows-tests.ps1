# Tests de non-regression Windows (chemin natif FindFirstFileExW + premier
# niveau GetFileAttributesExW). Usage :
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-windows-tests.ps1
# Couvre : jonction non suivie, dates frontieres exactes, dates < 1970
# observables a la racine ET en sous-repertoire, tailles/comptages.
$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "..\dirsize.exe"
if (-not (Test-Path $exe)) { Write-Output "FAIL: dirsize.exe absent (le compiler d'abord)"; exit 1 }

$script:pass = 0; $script:fail = 0
function Check($name, $cond) {
    if ($cond) { Write-Output "PASS: $name"; $script:pass++ }
    else       { Write-Output "FAIL: $name"; $script:fail++ }
}

$W = Join-Path $env:TEMP "dirsize-tests"
Remove-Item -Recurse -Force $W -ErrorAction SilentlyContinue

# --- fixtures ------------------------------------------------------------
# racine_1950 : date < 1970 AU PREMIER NIVEAU (chemin GetFileAttributesExW),
#               choisie hiver (pas d'ambiguite DST) et plus ancienne de l'arbre.
# d/sub_1960  : date < 1970 en sous-repertoire (chemin natif).
# d/sub_2020  : date pile sur la seconde (frontiere de troncature).
# target + j  : cible de jonction ; j ne doit jamais etre suivi ni compte.
foreach ($d in "t\d", "t\target", "t\vide") {
    New-Item -ItemType Directory -Force (Join-Path $W $d) | Out-Null
}
[IO.File]::WriteAllBytes("$W\t\racine_1950.txt", (New-Object byte[] 10))
[IO.File]::WriteAllBytes("$W\t\d\sub_1960.txt",  (New-Object byte[] 20))
[IO.File]::WriteAllBytes("$W\t\d\sub_2020.txt",  (New-Object byte[] 30))
foreach ($n in "a", "b", "c") {
    [IO.File]::WriteAllBytes("$W\t\target\$n.bin", (New-Object byte[] 1000))
}
(Get-Item "$W\t\racine_1950.txt").LastWriteTime = [datetime]'1950-11-30 08:45:00'
(Get-Item "$W\t\d\sub_1960.txt").LastWriteTime  = [datetime]'1960-05-04 12:00:00'
(Get-Item "$W\t\d\sub_2020.txt").LastWriteTime  = [datetime]'2020-01-15 10:30:00'
New-Item -ItemType Junction -Path "$W\t\j" -Target "$W\t\target" | Out-Null

& $exe "$W\t" "$W\out.csv" | Out-Null
if (-not (Test-Path "$W\out.csv")) { Write-Output "FAIL: aucun CSV produit"; exit 1 }

$rows = @{}
foreach ($l in (Get-Content "$W\out.csv" | Select-Object -Skip 1)) {
    $name = ($l -split ';')[0].Trim('"')
    $rows[$name] = $l
}

# --- assertions ----------------------------------------------------------
Check "jonction enfant ignoree (aucune ligne 'j')"         (-not $rows.ContainsKey("j"))
Check "cible comptee une seule fois (3000 o / 3 fichiers)" ($rows["target"] -match '"target";3000;3;')
Check "sous-rep : dates exactes 1960 et 2020"              ($rows["d"] -eq '"d";50;2;1960-05-04 12:00:00;2020-01-15 10:30:00')
Check "TOTAL : taille 3060 o / 6 fichiers"                 ($rows["TOTAL (t)"] -match ';3060;6;')
Check "TOTAL : plus ancien = fichier racine 1950 (pre-1970, 1er niveau)" ($rows["TOTAL (t)"] -match ';3060;6;1950-11-30 08:45:00;')
Check "repertoire vide : dates vides"                      ($rows["vide"] -eq '"vide";0;0;;')

Remove-Item -Recurse -Force $W -ErrorAction SilentlyContinue
Write-Output "----------------------------------------"
Write-Output "Resultat : $script:pass pass, $script:fail fail"
if ($script:fail -gt 0) { exit 1 }
