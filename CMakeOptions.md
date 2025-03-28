# The axmol CMake options

## The options for axmol engine
- AX_BUILD_TESTS: whether build test porojects: cpp-tests, lua-tests, fairygui-tests, default: `TRUE`
- AX_ENABLE_XXX for core feature: 
  - AX_ENABLE_MSEDGE_WEBVIEW2: whether enable msedge webview2, default: `TRUE`
  - AX_ENABLE_MFMEDIA: whether enable microsoft media foundation for windows video player support, default: `TRUE`
  - AX_ENABLE_VLC_MEDIA: whether enable libvlc media, default: `TRUE on Linux`, `FALSE on Windows`, not support other platforms
- AX_USE_XXX:
  - AX_USE_ALSOFT: whether use openal-soft for all platforms
    - Apple platform: Use openal-soft instead system deprecated: `OpenAL.framework`
    - Other platforms: Always use openal-soft even this option not enabled
  - AX_USE_LUAJIT: whether use luajit, default: `FALSE`, use plainlua
- AX_ENABLE_EXT_XXX for extensions
  - AX_ENABLE_EXT_GUI: the traditional GUI extension, default: `TRUE`
  - AX_ENABLE_EXT_ASSETMANAGER: the assetmanager extension, default: `TRUE`
  - AX_ENABLE_EXT_PARTICLE3D: the particle3d extension, default: `TRUE`
  - AX_ENABLE_EXT_PHYSICS_NODE: the physics_node extension, default: `TRUE`
  - AX_ENABLE_EXT_SPINE: the spine extension, default: `TRUE`
  - AX_ENABLE_EXT_DRAGONBONES: the dragonbones extension: `TRUE`
  - AX_ENABLE_EXT_COCOSTUDIO: the cocosstudio extension for load .csb: `TRUE`
  - AX_ENABLE_EXT_FAIRYGUI: the fairygui extension, default: `TRUE`
  - AX_ENABLE_EXT_IMGUI: the imgui extension, support macos,win,linux,android, default: `TRUE` 
  - AX_ENABLE_EXT_LIVE2D: the live2d extension, default: `FALSE` 
  - AX_ENABLE_EXT_EFFEKSEER: the effekseer extension, default: `FALSE` 
- AX_WITH_XXX: usually user don't need care it
- AX_VS_DEPLOYMENT_TARGET: specify windows store deploy target, default: `10.0.17763.0`
- AX_USE_COMPAT_GL: whether use compat gl as renderer backend, default: FALSE
  - win32: whether use ANGLE GLES backend
  - osx: whether use OpenGL instead Metal backend
  - ios/tvos: whether use GLES instead Metal backend
- AX_GLES_PROFILE: speicify GLES profile version for GLES backend, valid value `200`, `300`
- AX_WASM_THREADS: specify wasm thread count, valid value: number: `>=0` , string: must be: `auto` or `navigator.hardwareConcurrency`, 
   - number: explicit set thread count, `0` means disable wasm thread support
   - string: 
     - `navigator.hardwareConcurrency`: will be emitted in the JS code which will use the number of cores the browser reports
     - `auto`: Use cmake to detect host processor count
default is: `navigator.hardwareConcurrency`

## The options for axmol apps

- AX_PREBUILT_DIR: specific the prebuilt dir (relative to `AX_ROOT`), it's very useful for fast linking apps with prebuilt engine libs
