# Opt-in desktop video settings

The desktop host exposes standard SNES pixel proportions when
`SnesDesktopHostGame.display_aspect_supported = 1` and the GLSL picker when
`shader_supported = 1`. Both default to zero. Custom `aspect_labels` retain
their game-defined meaning and do not opt into SNES DisplayAspect persistence.

Bundle the shared CRT Soft, LCD Grid, Sharp and Warm Composite presets with
`snesrecomp_target_shader_presets(target)` in CMake. MSBuild hosts may import
`runner/msbuild/shader_presets.targets`. These copy the catalog next to the
executable under `assets/shaders`, preserving user-added presets. Release
packaging must include that directory along with other launcher assets.

Custom hosts can use `desktop/launcher_video.h`:

```c
SnesLauncherVideo_Configure(&settings, &game_info, 1, 1,
                           config.display_aspect, config.shader);
```

Apply the returned `aspect_index` (clamped with `SnesDisplayAspect_Clamp`) and
copy `shader_path` into host-owned storage before starting the renderer.
Also apply `output_method`: selecting a shader selects the OpenGL presenter.
The shared desktop host already does this. recomp-ui versions with
`RECOMP_LAUNCHER_HAS_SNES_DISPLAY_ASPECT` save/load standard DisplayAspect
and Shader on Play and Quit, including custom hosts that return early on Quit.
Older recomp-ui versions still work with the shared host's config writer.

`[Graphics] DisplayAspect` accepts `4:3`, `8:7` and `1:1` (legacy numeric and
named aliases also work). Custom config readers should call
`SnesDisplayAspect_Parse`; writers should use `SnesDisplayAspect_Name`.
The native 256x224 picture's horizontal pixel ratios are 7:6, 1:1 and 7:8.

Bundled shader choices use stable `assets/shaders/...` resource paths. The
shared GLSL loader resolves these against the executable, including a fresh
AppImage mount on each launch. Custom file paths retain their normal meaning.

Adaptive renderers should call `SnesDisplayAspect_ComputeAdaptiveFrame` with
their native dimensions, maximum composition width, desired screen ratio and
selected DisplayAspect. Use `SnesDisplayAspect_FitViewport` to present the
returned aspect. Fixed widescreen targets and fit-to-window both preserve the
selected pixel proportions. Widths stay centered/even; native and capacity
bounds box the picture. A disabled widescreen mod uses native width as its
maximum, so DisplayAspect still controls the stock picture.

Validation: configure `tests/host/video_settings` with `-DRECOMP_UI_ROOT=...`,
build, and run CTest. MMX and SMW also exercise their renderer adapters.
