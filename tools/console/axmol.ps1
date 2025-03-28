param(
    [switch]$help,
    [switch]$uninst
)
# pwsh function alias
Set-Alias println Write-Host

$myRoot = $PSScriptRoot

# 0: windows, 1: linux, 2: macos
# $IsWin = $IsWindows -or ("$env:OS" -eq 'Windows_NT')

# parse engine version
$AX_ROOT = (Resolve-Path $myRoot/../..).Path
$axver_file = (Resolve-Path $AX_ROOT/core/axmolver.h.in).Path
$content = ($(Get-Content -Path $axver_file) | Select-String 'AX_VERSION_STR')
$axmolVersion = $content[0].Line.Split(' ')[2].Replace('"', '')

$git_prog = (Get-Command 'git' -ErrorAction SilentlyContinue).Source
if ($git_prog) {
    $branchName = $(git -C $AX_ROOT branch --show-current)
    if ($branchName -eq 'dev') {
        $commitHash = $(git -C $AX_ROOT rev-parse --short=7 HEAD)
        $axmolVersion += "-$commitHash"
    }
}

$pwsh_ver = $PSVersionTable.PSVersion.ToString()

# tool usage
$tool_usage = @'
axmol-{0} console: A command line tool for axmol engine.

Using PowerShell {1}

Available commands:
        new          Creates a new project.
        build        Compile projects to binary.
        deploy       Compile and deploy a project to a device/simulator.
        run          Compiles, deploy and run project on the target.

Available arguments:
        -h           Show this help information.

Example:
        axmol new --help
        axmol run --help
'@ -f $axmolVersion, $pwsh_ver

$cmdName = $args[0]
if (!$cmdName) {
    println $tool_usage
    return
}

if ($IsMacOS) {
    function find_simulator_id($plat, $deviceName = '') {
        $selStr = @{ios = 'iPhone'; tvos = 'Apple TV'}[$plat]
        [array]$emulators = (xcrun xctrace list devices | Select-String $selStr)
        foreach($emulator in $emulators) {
            $emuInfoStr = $emulator.Line
            if ($emuInfoStr.StartsWith($deviceName) -or !$deviceName) {
                return ($emuInfoStr -split "\(|\)")[3], $emuInfoStr
            }
        }
    }

    function get_bundle_id($appPath) {
        $plistFile = Join-Path $appPath 'Info.plist'
        $appInfo = ConvertFrom-Json (plutil -convert json -o - $plistFile)
        return $appInfo.CFBundleIdentifier
    }
}

function axmol_build() {
    $sub_args = $args
    println $sub_args
    $build_script = Join-Path $AX_ROOT 'build.ps1'
    if ("$args".IndexOf('-d') -ne -1) {
        # have proj dir
        . $build_script @sub_args
    }
    else {
        . $build_script @sub_args -d (Get-Location).Path
    }
}

function axmol_deploy() {
    $sub_args = $args
    . axmol_build @sub_args
    if ($options.p -eq 'winuwp') {
        $appxManifestFile = Join-Path $options.d "$BUILD_DIR/bin/$cmake_target/$optimize_flag/Appx/AppxManifest.xml"
        [XML]$appxManifest = Get-Content $appxManifestFile
        $appxIdentity = $appxManifest.Package.Identity.Name
        $appxPkgFullName = (powershell -Command "(Get-AppxPackage -Name '$appxIdentity' | Select-Object -Unique 'PackageFullName').PackageFullName")
        if ($appxPkgInfo ) {
            if ($uninst) {
                println "Uninstalling $appxPkgFullName ..."
                powershell -Command "Remove-AppxPackage -Package '$appxPkgFullName'"
                powershell -Command "Add-AppxPackage -Register '$appxManifestFile'"
            }
        }
        else {
            powershell -Command "Add-AppxPackage -Register '$appxManifestFile'"
        }
        $appxPkgName = (powershell -Command "(Get-AppxPackage -Name '$appxIdentity' | Select-Object -Unique 'PackageFamilyName').PackageFamilyName")
        println "axmol: Deploy $cmake_target done: $appxPkgName"
    }
    elseif($options.p -eq 'win32') {
        $win32exePath = Join-Path $options.d "$BUILD_DIR/bin/$cmake_target/$optimize_flag/$cmake_target.exe"
        println "axmol: Deploy $cmake_target done: $win32exePath"
    }
    elseif ($options.p -eq 'android') {
        $optimize_flag_lower = $optimize_flag.ToLower()
        $apkDir = Join-Path $proj_dir "proj.android/app/build/outputs/apk/$optimize_flag_lower"
        $apkName = (Get-ChildItem -Path "$apkDir/*.apk").Name
        $apkFullPath = Join-Path $apkDir $apkName
        $androidManifestFile = Join-Path $proj_dir 'proj.android/app/AndroidManifest.xml'
        [XML]$androidManifest = Get-Content $androidManifestFile
        $androidPackage = $androidManifest.manifest.package
        $androidActivity = $androidManifest.manifest.application.activity.name
        adb install -t -r $apkFullPath
        if ($?) {
            println "axmol: Deploy $cmake_target done: $androidPackage/$androidActivity"
        }
    }
    elseif ($options.p -eq 'ios' -or $options.p -eq 'tvos') {
        if ($options.a -eq 'x64') {
            $ios_app_path = Join-Path $proj_dir "$BUILD_DIR/bin/$cmake_target/$optimize_flag/$cmake_target.app"
            $ios_bundle_id = get_bundle_id($ios_app_path)

            println 'axmol: Finding avaiable simualtor ...'
            $ios_simulator_id, $simulator_info = find_simulator_id($options.p)

            println "axmol: Booting $simulator_info ..."
            xcrun simctl boot $ios_simulator_id '--arch=x86_64'
            println "axmol: Installing $ios_app_path ..."
            xcrun simctl install $ios_simulator_id $ios_app_path
            println "axmol: Deploy $cmake_target done: $ios_bundle_id"
        } else { # ios device

        }
    }
}

function axmol_run() {
    $sub_args = $args
    . axmol_deploy @sub_args
    if ($options.p -eq 'winuwp') {
        explorer.exe shell:AppsFolder\$appxPkgName!App
    }
    elseif($options.p -eq 'win32') {
        explorer.exe $win32exePath
    }
    elseif($options.p -eq 'android') {
        adb shell am start -n "$androidPackage/$androidActivity"
    }
    elseif($options.p -eq 'ios') {
        if ($ios_bundle_id) {
            println "Launching $cmake_target ..."
            xcrun simctl launch $ios_simulator_id $ios_bundle_id 
        }
    }
    elseif($options.p -eq 'osx') {
        $launch_macapp = Join-Path $proj_dir "$BUILD_DIR/bin/$cmake_target/$optimize_flag/$cmake_target.app/Contents/MacOS/$cmake_target"
        & $launch_macapp
    }
    elseif($options.p -eq 'linux') {
        $launch_linuxapp = Join-Path $proj_dir "$BUILD_DIR/bin/$cmake_target/$cmake_target"
        println "axmol: Launching $launch_linuxapp ..."
        Start-Process -FilePath $launch_linuxapp -WorkingDirectory $(Split-Path $launch_linuxapp -Parent)
    }
    elseif($options.p -eq 'wasm') {
        $launch_wasmapp = Join-Path $proj_dir "$BUILD_DIR/bin/$cmake_target/$cmake_target.html"
        println "axmol: Launching $launch_wasmapp ..."
        emrun $launch_wasmapp
    }
    println "axmol: Launch $cmake_target done, target platform is $(options.p)"
}

$builtinPlugins = @{
    new    = @{
        proc  = (Join-Path $myRoot 'axmol_new.ps1');
        usage = @"
usage: axmol new -p org.axmol.hellocpp -d path/to/project -l --portrait cpp <ProjectName>
Creates a new project.

positional arguments:
    PROJECT_NAME          Set the project name.

options:
    -h                  Show this help message and exit
    -p PACKAGE_NAME
                        Set a package name for project.
    -d DIRECTORY
                        Set the path where to place the new project.
    -l {cpp,lua}, {cpp,lua}
                        Major programming language you want to use, should be [cpp | lua]
    --portrait
                        Set the project be portrait.
"@;
    };
    build  = @{
        proc  = ${function:axmol_build};
        usage = @"
axmol: axmol build -p win32 -a x64

Build projects to binary.

options:
  -h: show this help message and exit
  -p: build target platform: win32,winuwp,linux,android,osx,ios,tvos,watchos
      for android: will search ndk in sdk_root which is specified by env:ANDROID_HOME first, 
      if not found, by default will install ndk-r16b or can be specified by option: -cc 'ndk-r23c'
  -a: build arch: x86,x64,armv7,arm64; for android can be list by ';', i.e: 'arm64;x64'
  -cc: toolchain: for win32 you can specific -cc clang to use llvm-clang, please install llvm-clang from https://github.com/llvm/llvm-project/releases
  -xc: additional cmake options: i.e.  -xc '-Dbuild','-DCMAKE_BUILD_TYPE=Release'
  -xb: additional cross build options: i.e. -xb '--config','Release'
  -nb: no build, only generate natvie project file (vs .sln, xcodeproj)
  -d: specify project dir to compile, i.e. -d /path/your/project/
 examples:
   - win32: 
     - axmol build -p win32
     - axmol build -p win32 -cc clang
   - winuwp: axmol build -p winuwp
   - linux: axmol build -p linux
   - android: 
     - axmol build -p android -a arm64
     - axmol build -p android -a 'arm64;x64'
   - osx: 
     - axmol build -p osx -a x64
     - axmol build -p osx -a arm64
   - ios: axmol build -p ios -a x64
   - tvos: axmol build -p tvos -a x64
"@;
    };
    deploy = @{
        proc  = ${function:axmol_deploy};
        usage = @"
usage: axmol install -p win32 -a x64

Build and install a project to a device/simulator.

options:
  -p: build target platform: win32,winuwp,linux,android,osx,ios,tvos,watchos
      for android: will search ndk in sdk_root which is specified by env:ANDROID_HOME first, 
      if not found, by default will install ndk-r16b or can be specified by option: -cc 'ndk-r23c'
  -a: build arch: x86,x64,armv7,arm64; for android can be list by ';', i.e: 'arm64;x64'
  -cc: toolchain: for win32 you can specific -cc clang to use llvm-clang, please install llvm-clang from https://github.com/llvm/llvm-project/releases
  -xc: additional cmake options: i.e.  -xc '-Dbuild','-DCMAKE_BUILD_TYPE=Release'
  -xb: additional cross build options: i.e. -xb '--config','Release'
  -nb: no build, only generate natvie project file (vs .sln, xcodeproj)
  -d: specify project dir to compile, i.e. -d /path/your/project/
"@;
    };
    run    = @{
        proc  = ${function:axmol_run};
        usage = @"
usage: axmol run -p win32 -a x64
Build, deploy and run project on the target.

options:
  -p: build target platform: win32,winuwp,linux,android,osx,ios,tvos,watchos
      for android: will search ndk in sdk_root which is specified by env:ANDROID_HOME first, 
      if not found, by default will install ndk-r16b or can be specified by option: -cc 'ndk-r23c'
  -a: build arch: x86,x64,armv7,arm64; for android can be list by ';', i.e: 'arm64;x64'
  -cc: toolchain: for win32 you can specific -cc clang to use llvm-clang, please install llvm-clang from https://github.com/llvm/llvm-project/releases
  -xc: additional cmake options: i.e.  -xc '-Dbuild','-DCMAKE_BUILD_TYPE=Release'
  -xb: additional cross build options: i.e. -xb '--config','Release'
  -nb: no build, only generate natvie project file (vs .sln, xcodeproj)
  -d: specify project dir to compile, i.e. -d /path/your/project/
"@
    }
}

$plugin = $builtinPlugins[$cmdName]
if (!$plugin) {
    "axmol: command '$cmdName' not found"
    println $tool_usage
    return
}

# -h will consumed by param
$sub_args = $args[1..($args.Count - 1)]
if (!$sub_args[0] -or $help) {
    println $plugin.usage
    return
}

. $plugin.proc @sub_args
