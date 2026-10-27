# Build the FM-1 firmware

The target is JieLi AC791N / pi32v2. The source builds an application, update loader and `.fwsc` package. The JieLi AC79 SDK and Linux x86-64 toolchain are external dependencies and are **not** included in this repository.

On Linux x86-64:

```sh
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
  https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain"
export AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
./build.sh --release 0.26.19
```

On Windows, `build.ps1 -Toolchain <directory> -Sdk <directory>` uses Docker to run the Linux compiler. The builder pins critical SDK file hashes and checks app/RAM size and register/flash guards. The explicit `--release 0.26.19` build above writes `build/terkinstein-0.26.19.fwsc`; a build without `--release` writes `build/terkinstein.fwsc` with the source's default version. The package and app use `FM-1_986`, the loader `ota-FM-1_986`; the shared default and publication checks are documented in IDENTITY.md.

For a desktop sound and screen preview, use a C compiler, Python and Pillow:

```sh
python3 -m pip install pillow
python3 tools/preview.py --test
```

The focused twenty-group firmware suite is `python3 tools/check_host.py`.
Use `--asan` with an AddressSanitizer-capable compiler (MSVC, clang or gcc).
On Windows, run from the Visual Studio developer environment or pass `--cc`.
The runner uses fake files only; it never opens MIDI or an audio device.

Check authored assets with `python3 tools/generate_presets.py --check`,
`python3 tools/make_homunculus.py --check` and
`python3 tools/make_homunculus_gif.py --check`. Pillow is required for images.

`tests/` contains host C checks and timed control scripts. A host preview proves code paths, not device timing or MIDI/USB behavior. The splash image and preset catalog are in `assets/`; generated C headers in `firmware/gen/` are tracked so the source is complete.
