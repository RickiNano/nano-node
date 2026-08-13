$ErrorActionPreference = "Continue"

# Git ships a coreutils `link.exe` that shadows the MSVC linker of the same name
# whenever CMake configures from a bash shell, because bash puts its own /usr/bin
# ahead of the MSVC toolchain on PATH.
$gitLink = "C:\Program Files\Git\usr\bin\link.exe"
if (Test-Path $gitLink) {
    Remove-Item $gitLink -Force
}
