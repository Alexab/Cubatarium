# Create upload keystore and keystore.properties for Play Store release signing.
$ErrorActionPreference = "Stop"
$androidDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$keystorePath = Join-Path $androidDir "cubatarium-upload.jks"
$propsPath = Join-Path $androidDir "keystore.properties"

if (Test-Path $propsPath) {
    Write-Host "keystore.properties already exists: $propsPath"
    exit 0
}

$keytool = Get-Command keytool -ErrorAction SilentlyContinue
if (-not $keytool) {
    Write-Error "keytool not found. Install JDK and ensure keytool is on PATH."
}

if (-not (Test-Path $keystorePath)) {
    Write-Host "Generating upload keystore at $keystorePath"
    Write-Host "You will be prompted for keystore and key passwords (remember them for Play upload key)."
    & keytool -genkeypair -v `
        -keystore $keystorePath `
        -keyalg RSA `
        -keysize 2048 `
        -validity 10000 `
        -alias upload
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host ""
Write-Host "Create keystore.properties manually from keystore.properties.example"
Write-Host "  copy keystore.properties.example keystore.properties"
Write-Host "Then set storePassword and keyPassword to match your keystore."
