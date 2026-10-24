#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Platform-isolation guard (docs/platform-isolation.md).
#
# WordJewelsSolver branches on the operating system in CMake, never in source: each
# platform's implementations live in libs/platforms/<name>/, and that
# platform's platform.cmake (selected once, in cmake/Platform.cmake) says which
# of them are compiled and what they link against. Adding a platform is a new
# directory plus one branch in the selector. That only stays true if nobody
# reaches for an #ifdef, an OS header or a runtime OS test in shared code - the
# path of least resistance the moment a second platform misbehaves - hence this
# check. It is the same rule iclforge holds with tools/checks/
# check_platform_macros.ps1 (no preprocessor conditional of any kind), extended
# with the leaks that rule cannot see: OS APIs reached without a conditional,
# OS tests in CMake, and platform files nothing compiles.
#
# Five rules, each reported with file:line:
#
#   1. NO PREPROCESSOR CONDITIONALS in any first-party tree (src/, tests/,
#      tools/) - #if, #ifdef, #ifndef, #elif, #else, #endif. Deliberately
#      broader than "no OS macros": an #if 0, a feature-flag #ifdef and a
#      compiler test are just as unwelcome, and zero is a far easier line to
#      hold than "only the justified ones". Variation comes from the build
#      graph (a source file per variant, chosen in CMake) or from a value CMake
#      passes in with target_compile_definitions - never from the preprocessor.
#      #define, #include and #pragma are not conditionals and are not matched;
#      the code uses #pragma once. This rule has no exemption, the platform
#      directories included: they are already chosen by CMake.
#
#   2. NO OS HEADERS OR OS-QUERYING APIs outside the platform section
#      (libs/platforms/ and tests/platform/): not <windows.h>, <unistd.h>,
#      <sys/...>, Objective-C, Android or Emscripten headers, and not QSysInfo,
#      QOperatingSystemVersion, Q_OS_*, QJniObject, platformName() or QML's
#      Qt.platform. Code that needs an OS service declares an interface in
#      libs/platforms/interfaces/ and gets one implementation per platform.
#
#   3. EVERY PLATFORM TRANSLATION UNIT IS WIRED. Each libs/platforms/<name>/
#      directory (other than detail/ and posix/, which are not platforms) has
#      a platform.cmake, and every .cpp/.mm in the section is named, outside a
#      comment, by one of them - a file nothing compiles is a platform that
#      was "added" in the source tree and never in CMake.
#
#   4. NO OS TESTS IN CMAKE outside cmake/Platform.cmake, the platform
#      fragments, the platform section's own tests in tests/platform/*.cmake (each
#      is about one platform and cannot name it otherwise), the vcpkg triplets in
#      cmake/triplets/ (a triplet is a platform's name and flags) and the vcpkg
#      overlay ports in cmake/vcpkg-ports/ (a third party's port, kept as it
#      is): no WIN32, APPLE, ANDROID, IOS, UNIX, LINUX, MINGW, CYGWIN,
#      EMSCRIPTEN, CMAKE_SYSTEM_NAME or $<PLATFORM_ID:...>. (Compiler tests - MSVC,
#      CXX_COMPILER_ID - are a different axis and are not matched, nor are
#      property names such as WIN32_EXECUTABLE.)
#
#   5. NO #define OR #undef in any first-party C++ tree. Platform directories
#      included: what they need from the build arrives as a compile definition
#      CMake passes in, not as a macro the source declares.
#
# Not scanned, deliberately: third_party/ and deps/ (vendored code, with its own
# licence and conventions), anything git ignores (build output, generated
# headers), and shader sources (GLSL has no operating system to branch on).
#
# Usage:  ./tools/checks/check_platform_isolation.ps1 [-Root <repo-root>]
# Exit:   0 = clean, 1 = violation(s) found, 2 = bad invocation.

param(
    [string]$Root = $PWD
)

$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path -LiteralPath $Root).Path
$platformRoot = Join-Path $Root 'libs/platforms'
if (-not (Test-Path -LiteralPath $platformRoot)) {
    Write-Error "No libs/platforms/ under '$Root'. Pass -Root <repo-root>."
    exit 2
}

# --- what is scanned --------------------------------------------------------

# Every first-party tree. Optional, each, so a checkout of an older tag still
# has something to scan. The top-level cmake/ and CMakeLists.txt are scanned
# for rule 4 only (they hold no C++).
$scanRoots = @('libs', 'apps', 'tests', 'tools', 'cmake') |
    ForEach-Object { Join-Path $Root $_ } |
    Where-Object { Test-Path -LiteralPath $_ }

$cppExtensions = @('.h', '.hh', '.hpp', '.hxx', '.inl', '.ipp', '.tpp', '.inc',
    '.c', '.cc', '.cpp', '.cxx', '.cppm', '.ixx', '.cu', '.cuh', '.m', '.mm')
$qmlExtensions = @('.qml', '.js')

function Get-RelativePath([string]$path) {
    return [System.IO.Path]::GetRelativePath($Root, $path).Replace('\', '/')
}

# A generated header's or source's template (version.hpp.in, x.cpp.in) is
# source too.
function Test-IsCppSource([System.IO.FileInfo]$file) {
    if ($cppExtensions -contains $file.Extension.ToLowerInvariant()) { return $true }
    return $file.Name -match '\.(h|hh|hpp|hxx|inl|ipp|tpp|c|cc|cpp|cxx|m|mm)\.in$'
}

function Test-IsCMake([System.IO.FileInfo]$file) {
    return $file.Name -ieq 'CMakeLists.txt' -or $file.Extension -ieq '.cmake'
}

$candidates = @(Get-ChildItem -LiteralPath $scanRoots -Recurse -File |
    Where-Object {
        (Test-IsCppSource $_) -or (Test-IsCMake $_) -or ($qmlExtensions -contains $_.Extension.ToLowerInvariant())
    })
$rootCMake = Join-Path $Root 'CMakeLists.txt'
if (Test-Path -LiteralPath $rootCMake) { $candidates += Get-Item -LiteralPath $rootCMake }

# Ask git which files are source rather than pattern-matching directory names:
# .gitignore already carries that answer (build trees, generated headers) and
# stays the single place it is written down. --others picks up a file that is
# new and not yet committed, which is exactly the case worth catching before it
# lands. -z makes the output NUL-separated and unquoted, so a path with a space
# or a non-ASCII byte is compared as it is.
$sourcePaths = $null
$previousNativePreference = $PSNativeCommandUseErrorActionPreference
try {
    # PowerShell 7.3+ turns a non-zero native exit code into a terminating
    # error under 'Stop'; that translation is switched off around the call.
    $PSNativeCommandUseErrorActionPreference = $false
    $listed = git -C $Root ls-files -z --cached --others --exclude-standard -- $scanRoots $rootCMake 2>$null
    if ($LASTEXITCODE -eq 0) {
        $sourcePaths = [System.Collections.Generic.HashSet[string]]::new(
            [string[]]@((@($listed) -join '') -split "`0" | Where-Object { $_ }),
            [System.StringComparer]::OrdinalIgnoreCase)
    }
} catch {
    # No git on PATH, or not a work tree (an exported tarball): scanning
    # everything is the safe direction to fail in - noisier, never quieter.
    $sourcePaths = $null
} finally {
    $PSNativeCommandUseErrorActionPreference = $previousNativePreference
}

$scannedBeforeFilter = @($candidates).Count
if ($null -ne $sourcePaths -and $sourcePaths.Count -gt 0) {
    $candidates = @($candidates | Where-Object { $sourcePaths.Contains((Get-RelativePath $_.FullName)) })
}
$skipped = $scannedBeforeFilter - @($candidates).Count

# --- the rules --------------------------------------------------------------

# Rule 1. Any conditional-compilation directive (a leading /* comment */ does
# not hide one).
$directivePattern = '^\s*(?:/\*.*?\*/\s*)*#\s*(if|ifdef|ifndef|elif|elifdef|elifndef|else|endif)\b'

# Rule 5. No #define or #undef in first-party C++. Constants are constexpr,
# helpers are functions or templates, and a value the build has to hand the
# program is generated into a header by CMake (configure_file) or passed with
# target_compile_definitions. #include and #pragma once are not matched.
$definePattern = '^\s*(?:/\*.*?\*/\s*)*#\s*(define|undef)\b'

# Rule 2. Headers that exist on one operating system, or one family of them.
$osHeaderPattern = '^\s*(?:/\*.*?\*/\s*)*#\s*(include|import)\s*[<"](' + (@(
        # Windows
        'windows\.h', 'winsock2?\.h', 'ws2tcpip\.h', 'winbase\.h', 'winnt\.h', 'winerror\.h',
        'shlobj(_core)?\.h', 'shlwapi\.h', 'shellapi\.h', 'psapi\.h', 'tchar\.h', 'comdef\.h',
        'dshow\.h', 'objbase\.h', 'ole2?\.h', 'oleauto\.h', 'dbghelp\.h', 'io\.h', 'direct\.h',
        'process\.h', 'd3d\w*\.h', 'dxgi\w*\.h', 'mf\w+\.h', 'wrl(/[^>"]*)?',
        # POSIX and Unix
        'unistd\.h', 'dlfcn\.h', 'termios\.h', 'poll\.h', 'fcntl\.h', 'pwd\.h', 'grp\.h',
        'pthread\.h', 'semaphore\.h', 'sched\.h', 'dirent\.h', 'glob\.h', 'libgen\.h',
        'netdb\.h', 'ifaddrs\.h', 'execinfo\.h', 'utime\.h', 'syslog\.h', 'jni\.h',
        'sys/[^>"]+', 'linux/[^>"]+', 'arpa/[^>"]+', 'netinet/[^>"]+', 'net/[^>"]+',
        # Apple, Android, X11, the browser (Emscripten)
        'mach/[^>"]+', 'mach-o/[^>"]+', 'android/[^>"]+', 'TargetConditionals\.h',
        'emscripten\.h', 'emscripten/[^>"]+',
        'AppKit/[^>"]+', 'Cocoa/[^>"]+', 'CoreFoundation/[^>"]+', 'CoreMedia/[^>"]+',
        'AVFoundation/[^>"]+', 'Foundation/[^>"]+', 'UIKit/[^>"]+', 'objc/[^>"]+',
        'X11/[^>"]+', 'xcb/[^>"]+') -join '|') + ')[>"]'

# Rule 2. APIs whose whole purpose is to ask which OS this is, or to reach one
# OS's runtime. Matched on code with comments removed, so a comment that
# explains why something is NOT done with them is not a finding.
$osApiPattern = '\b(QSysInfo|QOperatingSystemVersion|QJniObject|QJniEnvironment|QNativeInterface|' +
    'Q_OS_[A-Z0-9_]+|Q_PROCESSOR_[A-Z0-9_]+)\b|\bplatformName\s*\(|\bQt\s*\.\s*platform\b|' +
    '\bQt\s*\[\s*["'']platform'

# Rule 4. A test of the operating system in CMake. The look-arounds keep
# CMAKE_HOST_WIN32 (the machine running CMake, not the target), WIN32_EXECUTABLE
# and QT_ANDROID_PACKAGE_SOURCE_DIR (property names) from matching.
$cmakeOsPattern = '(?<![A-Za-z0-9_])(WIN32|APPLE|ANDROID|IOS|UNIX|LINUX|MINGW|CYGWIN|EMSCRIPTEN)(?![A-Za-z0-9_])|' +
    'CMAKE_SYSTEM_NAME|\$<PLATFORM_ID'

$violations = [System.Collections.Generic.List[object]]::new()
function Add-Violation([string]$rule, [string]$path, [int]$line, [string]$text, [string]$hint) {
    $violations.Add([pscustomobject]@{ Rule = $rule; Path = $path; Line = $line; Text = $text; Hint = $hint })
}

$conditionalHint = 'Select the variant in CMake (a source file per platform, named in its platform.cmake) or pass a value in with target_compile_definitions.'
$osHint = 'Put the OS service behind an interface in libs/platforms/interfaces/ with one implementation per platform under libs/platforms/.'
$defineHint = 'Use constexpr, an inline function or a template; a build-time value comes from a header CMake generates with configure_file, or target_compile_definitions.'
$cmakeHint = 'Declare it in the platform''s libs/platforms/<name>/platform.cmake and read it as a variable or hook; only cmake/Platform.cmake tests the OS.'

$platformPrefixes = @('libs/platforms/', 'tests/platform/')

# The code of one line: block comments (which may span lines, hence $state) and
# trailing // comments removed. `$state.InBlock` is carried from line to line.
function Get-Code([string]$line, $state) {
    $code = [System.Text.StringBuilder]::new()
    $rest = $line
    while ($rest.Length -gt 0) {
        if ($state.InBlock) {
            $end = $rest.IndexOf('*/')
            if ($end -lt 0) { break }
            $rest = $rest.Substring($end + 2)
            $state.InBlock = $false
            continue
        }
        $block = $rest.IndexOf('/*')
        $lineComment = $rest.IndexOf('//')
        if ($lineComment -ge 0 -and ($block -lt 0 -or $lineComment -lt $block)) {
            [void]$code.Append($rest.Substring(0, $lineComment))
            break
        }
        if ($block -ge 0) {
            [void]$code.Append($rest.Substring(0, $block))
            [void]$code.Append(' ')
            $rest = $rest.Substring($block + 2)
            $state.InBlock = $true
            continue
        }
        [void]$code.Append($rest)
        break
    }
    return $code.ToString()
}

foreach ($file in $candidates) {
    $relative = Get-RelativePath $file.FullName
    $isPlatformSection = $false
    foreach ($prefix in $platformPrefixes) {
        if ($relative.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $isPlatformSection = $true
        }
    }
    $isCpp = Test-IsCppSource $file
    $isCMake = Test-IsCMake $file
    # A vcpkg triplet is a platform's name and flags, in a format that cannot
    # read a CMake variable (the same reason the presets and vcpkg.json carry
    # platform names), so cmake/triplets/ is as exempt as the fragments. So are the
    # platform section's own tests (tests/platform/*.cmake), each about one platform.
    # So is a vcpkg
    # overlay port (cmake/vcpkg-ports/): a copy of someone else's portfile, which tests
    # the platform to install the way that platform needs, and is not ours to rewrite.
    $isSelectorOrFragment = $relative -ieq 'cmake/Platform.cmake' -or
        $relative -match '^libs/platforms/[^/]+/platform\.cmake$' -or
        $relative -match '^tests/platform/[^/]+\.cmake$' -or
        $relative -match '^cmake/triplets/[^/]+\.cmake$' -or
        $relative -match '^cmake/vcpkg-ports/'

    $state = [pscustomobject]@{ InBlock = $false }
    $lineNumber = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
        $lineNumber++

        if ($isCMake) {
            # CMake comments are # to end of line; there are no block comments
            # worth tracking here.
            if ($isSelectorOrFragment) { continue }
            $cmakeCode = ($line -replace '\s#.*$', '').Trim()
            if ($cmakeCode.StartsWith('#') -or $cmakeCode.Length -eq 0) { continue }
            if ($cmakeCode -match $cmakeOsPattern) {
                Add-Violation 'cmake-os' $relative $lineNumber $cmakeCode $cmakeHint
            }
            continue
        }

        $trimmed = (Get-Code $line $state).Trim()
        if ($trimmed.Length -eq 0) { continue }

        if ($isCpp -and $trimmed -match $directivePattern) {
            Add-Violation 'conditional' $relative $lineNumber $line.Trim() $conditionalHint
            continue
        }
        if ($isCpp -and $trimmed -match $definePattern) {
            Add-Violation 'define' $relative $lineNumber $line.Trim() $defineHint
            continue
        }
        if ($isPlatformSection) { continue }

        if ($isCpp -and $trimmed -match $osHeaderPattern) {
            Add-Violation 'os-header' $relative $lineNumber $line.Trim() $osHint
        } elseif ($trimmed -match $osApiPattern) {
            Add-Violation 'os-query' $relative $lineNumber $line.Trim() $osHint
        }
    }
}

# Rule 3. Every platform directory is wired into CMake, and every translation
# unit in it is compiled by some platform's fragment.
$platformDirs = Get-ChildItem -LiteralPath $platformRoot -Directory
$fragmentText = ''
foreach ($dir in $platformDirs) {
    $fragment = Join-Path $dir.FullName 'platform.cmake'
    if (Test-Path -LiteralPath $fragment) {
        # A name that appears only in a comment is not wired.
        $fragmentText += (([System.IO.File]::ReadAllLines($fragment) |
                Where-Object { -not $_.TrimStart().StartsWith('#') }) -join "`n") + "`n"
    }
}
foreach ($dir in $platformDirs) {
    # detail/ is the shared helpers every platform links (apps/app/CMakeLists.txt
    # lists them itself); posix/ is the shared Unix implementations the
    # fragments name, and is not a platform of its own.
    if ($dir.Name -in @('detail', 'interfaces', 'posix')) { continue }
    $fragment = Join-Path $dir.FullName 'platform.cmake'
    if (-not (Test-Path -LiteralPath $fragment)) {
        Add-Violation 'unwired' (Get-RelativePath $dir.FullName) 1 "$($dir.Name)/ has no platform.cmake" `
            'Add platform.cmake (the contract is described at the top of cmake/Platform.cmake) and a branch for it in the selector.'
    }
}
foreach ($dir in $platformDirs) {
    if ($dir.Name -in @('detail', 'interfaces')) { continue }
    $units = Get-ChildItem -LiteralPath $dir.FullName -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.cpp', '.cc', '.cxx', '.mm') }
    foreach ($unit in $units) {
        $needle = (Get-RelativePath $unit.FullName).Substring('libs/platforms/'.Length)
        if (-not $fragmentText.Contains($needle)) {
            Add-Violation 'unwired' (Get-RelativePath $unit.FullName) 1 "$needle is named by no platform.cmake" `
                'List it in the platform.cmake of every platform that compiles it, or delete it.'
        }
    }
}

# --- report -----------------------------------------------------------------

if ($violations.Count -gt 0) {
    Write-Host ''
    Write-Host 'Platform-isolation violation(s): platform-specific code outside the platform section, or a platform CMake does not know about.' -ForegroundColor Red
    Write-Host 'The OS is a CMake decision (docs/platform-isolation.md); source files do not ask which one they are on.'
    Write-Host ''
    foreach ($v in $violations) {
        Write-Host ('  [{0}] {1}:{2}: {3}' -f $v.Rule, $v.Path, $v.Line, $v.Text)
        # GitHub Actions annotation; prints harmlessly when run locally.
        Write-Host ('::error file={0},line={1}::[{2}] {3}' -f $v.Path, $v.Line, $v.Rule, $v.Hint)
    }
    Write-Host ''
    Write-Host ('{0} violation(s) found.' -f $violations.Count) -ForegroundColor Red
    exit 1
}

$summary = "OK: no platform code outside libs/platforms and tests/platform, no OS tests in CMake outside the selector, and every platform file is wired ($(@($candidates).Count) files scanned"
if ($skipped -gt 0) {
    # Printed rather than left implicit: the git filter turning the check
    # green by excluding real source is the one failure that would not
    # announce itself, so what it removed is always on the record.
    $summary += ", $skipped skipped as build output or git-ignored"
}
Write-Host "$summary)." -ForegroundColor Green
exit 0
