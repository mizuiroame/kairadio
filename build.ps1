# Builds KaiRadio.nro with the kairadio-devkit Docker image (ounce-devkit plus
# FFmpeg, from ffmpeg/Dockerfile).
#   .\build.ps1           build the .nro
#   .\build.ps1 test      run the host tests with coverage
#   .\build.ps1 preview   render UI screenshots to tests\preview\build\*.ppm
#   .\build.ps1 clean     remove build output
param([ValidateSet('nro', 'test', 'preview', 'clean')][string]$Target = 'nro')

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$image = 'kairadio-devkit'

docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker image '$image' not found. Build it with: docker build -t $image ffmpeg (after the ounce-devkit image)"
}

$mount = "${project}:/build/kairadio"
switch ($Target) {
    'nro'     { docker run --rm -v $mount -w /build/kairadio $image sh -c 'sh third_party/verify.sh >/dev/null && make -j8' }
    'test'    { docker run --rm --security-opt seccomp=unconfined -v $mount -w /build/kairadio/tests $image make coverage }
    'preview' { docker run --rm -v $mount -w /build/kairadio/tests/preview $image make }
    'clean'   { docker run --rm -v $mount -w /build/kairadio $image sh -c 'make clean && make -C tests clean && make -C tests/preview clean' }
}
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
