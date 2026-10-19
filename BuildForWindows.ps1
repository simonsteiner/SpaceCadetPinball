#Run this script from the repository root with CMake 3.24+ and Visual Studio 2022 installed
#Each preset fetches SDL and links it statically, so the zips only hold the executable
$builds = [ordered]@{
    "windows-x64" = "SpaceCadetPinballx64Win.zip"
}

foreach ($preset in $builds.Keys) {
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Compress-Archive -Path ".\bin\Release\SpaceCadetPinball.exe" -DestinationPath ".\bin\$($builds[$preset])" -Force
}
