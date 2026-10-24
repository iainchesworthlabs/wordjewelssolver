# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The privacy rules of the first release, as far as the source tree can say
# whether they hold (docs/design/application.md, "Privacy, built in and
# checked"; docs/product-requirements.md, section 12.2). Frozen: no audio
# recording, no network capability, no export, a local store and nothing else.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/privacy/check_sources.cmake
#
# Scans first-party source (src/, cmake/, tests/, the top-level CMake files and
# any other directory that is not third-party or documentation) and any
# manifest in it for what each rule forbids:
#
#   audio     the classes, calls and permission strings that capture sound
#   network   the network classes and headers, a URL literal in code, the Qt
#             modules whose job is the network, the manifest permissions
#   export    printing, sharing, saving through a dialog, the clipboard, a URL
#             handed to the system, launching another program, and any write to
#             a file outside the store (libs/core/src/store.cpp is the one place)
#
# What it does not do, and why: it is a pattern scan, not a proof. Qt Qml and Qt
# Quick link Qt Network themselves, so "no Qt Network in the binary" cannot be
# the rule and the rule is "no capability and no use" (application.md). A
# comment line is skipped, so the design can be written about; code is not. The
# checks that need a built artefact (an Android package's permissions, an iOS
# property list, a web page's content-security policy) have nothing to check
# until those targets exist, and are in application.md as to be added with them.
#
# The patterns are what the rules are made of, and the self-test
# (tests/privacy/test_check_sources.cmake) holds each one to a violation it must
# find, so that a pattern that silently stops matching is caught.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

# A word of an identifier on both sides is not the name: Qt6::Network is, and
# QtNetworkish is not.
set(_before "(^|[^A-Za-z0-9_])")
set(_after "($|[^A-Za-z0-9_])")

set(_rule_count 0)

# wjs_rule(<id> <scope> <boundary> <advice> <name>...)
#   scope: ALL, SRC (src/ only), NOSTORE (src/ except the store's source) or
#          CODE (source code, not build files, manifests or metadata)
#   boundary: WORD (the name is not part of a longer identifier) or NONE (the
#             name ends in punctuation, and what follows is not for the rule)
function(wjs_rule id scope boundary advice)
    set(_alternatives "")
    foreach(_name IN LISTS ARGN)
        if(_alternatives)
            set(_alternatives "${_alternatives}|${_name}")
        else()
            set(_alternatives "${_name}")
        endif()
    endforeach()
    math(EXPR _next "${_rule_count} + 1")
    set(_rule_count ${_next} PARENT_SCOPE)
    set(_rule_${_next}_id "${id}" PARENT_SCOPE)
    set(_rule_${_next}_scope "${scope}" PARENT_SCOPE)
    set(_rule_${_next}_advice "${advice}" PARENT_SCOPE)
    if(boundary STREQUAL "NONE")
        set(_rule_${_next}_regex "(${_alternatives})" PARENT_SCOPE)
    else()
        set(_rule_${_next}_regex "${_before}(${_alternatives})${_after}" PARENT_SCOPE)
    endif()
endfunction()

# --- audio: nothing records sound -------------------------------------------
wjs_rule(audio.capture ALL WORD
    "No audio is recorded (12.2): sound is output only"
    "QAudioSource" "QAudioInput" "QMediaRecorder" "QAudioRecorder" "setAudioInput" "audioInput"
    "AudioInput" "MediaRecorder"
    "AVAudioRecorder" "AVAudioEngine" "AVCaptureAudioDataOutput" "AudioQueueNewInput"
    "waveInOpen" "IAudioCaptureClient" "snd_pcm_open" "pa_simple_new" "ma_device_type_capture"
    "AudioRecord" "RECORD_AUDIO" "NSMicrophoneUsageDescription" "device.audio-input")
# getUserMedia is how a page asks for a camera; asking for audio is the rule.
wjs_rule(audio.web ALL WORD
    "A page may ask for a camera, never for audio"
    "audio[ ]*:[ ]*true")

# --- network: no capability, no use ------------------------------------------
wjs_rule(network.classes ALL WORD
    "No network capability (12.2): no network class is used"
    "QNetworkAccessManager" "QNetworkRequest" "QNetworkReply" "QNetworkProxy"
    "QNetworkInformation" "QNetworkInterface" "QTcpSocket" "QTcpServer" "QUdpSocket"
    "QSslSocket" "QWebSocket" "QWebSocketServer" "QHttpServer" "QHttpMultiPart" "QRestAccessManager"
    "QDnsLookup" "QHostInfo" "QHostAddress" "QLocalSocket" "QNetworkDatagram"
    "QtNetwork" "XMLHttpRequest" "WebSocket" "EventSource"
    "libcurl" "curl_easy_init" "WSAStartup" "getaddrinfo" "gethostbyname")
wjs_rule(network.headers ALL WORD
    "No network header is included"
    "sys/socket.h" "netinet/in.h" "arpa/inet.h" "netdb.h" "winsock2.h" "ws2tcpip.h"
    "QtNetwork/[A-Za-z]+")
# A URL in code is a place to go to. A URL in a comment is prose and skipped.
wjs_rule(network.url CODE NONE
    "No URL literal in code: the application has nowhere to go"
    "\"https?://")
wjs_rule(network.modules ALL WORD
    "No Qt module whose job is the network is linked or found"
    "Qt6::Network" "Qt6::NetworkAuth" "Qt6::WebSockets" "Qt6::WebEngine[A-Za-z]*" "Qt6::WebView"
    "Qt6::HttpServer" "Qt6::Bluetooth" "Qt6::Nfc" "Qt6::Positioning"
    "Qt::Network" "Qt5::Network"
    "find_package[(]Qt6[^)]*(Network|NetworkAuth|WebSockets|WebEngineCore|WebEngineQuick|WebView|HttpServer|Bluetooth|Nfc)")
# The permissions and entitlements are named in two pieces: a CMake file that
# spells out the name of a platform reads as a test of the operating system to
# the platform-isolation check (docs/platform-isolation.md), and this is not one.
string(CONCAT _permission "andro" "id.permission.")
string(CONCAT _entitlement "com.ap" "ple.security.")
wjs_rule(network.manifest ALL WORD
    "No network permission or entitlement in a manifest or property list"
    "${_permission}INTERNET" "${_permission}ACCESS_NETWORK_STATE" "${_permission}ACCESS_WIFI_STATE"
    "${_entitlement}network.client" "${_entitlement}network.server" "NSAllowsArbitraryLoads")

# --- export: nothing leaves the device ---------------------------------------
wjs_rule(export.share ALL WORD
    "Zero export (12.2): no share sheet, print, mail, clipboard or file-save dialog"
    "QPrinter" "QPrintDialog" "QPrintPreviewDialog" "QPdfWriter" "QFileDialog" "QDesktopServices"
    "QClipboard" "QDrag" "QShareSheet" "QSaveFile"
    "UIActivityViewController" "NSSharingService" "UIPrintInteractionController" "ACTION_SEND"
    "FileDialog" "FolderDialog" "QtQuick.Dialogs" "Qt.labs.platform" "Qt.openUrlExternally"
    "ShellExecute" "ShellExecuteW")
wjs_rule(export.mail ALL NONE
    "Zero export (12.2): no mail"
    "mailto:")
wjs_rule(export.launch SRC WORD
    "No other program is started from the application"
    "QProcess" "CreateProcess" "CreateProcessW" "popen" "execv" "execvp" "posix_spawn")
wjs_rule(export.system SRC WORD
    "No other program is started from the application"
    "std::system")
# The store is the one file the application writes: its source is the one place
# that opens a file for writing or changes the file system.
wjs_rule(export.write NOSTORE WORD
    "The application writes one file, the store, and only libs/core/src/store.cpp opens it"
    "std::ofstream" "std::fstream" "std::wofstream" "_wfopen" "freopen" "QFile::WriteOnly"
    "QIODevice::WriteOnly" "QIODevice::Append" "QIODevice::ReadWrite" "QIODeviceBase::WriteOnly"
    "QIODeviceBase::Append" "QIODeviceBase::ReadWrite" "QTextStream"
    "std::filesystem::copy" "std::filesystem::copy_file" "std::filesystem::rename"
    "std::filesystem::remove" "std::filesystem::remove_all" "std::filesystem::create_directories"
    "std::filesystem::create_directory" "std::filesystem::create_symlink"
    "std::filesystem::resize_file" "std::filesystem::permissions")
wjs_rule(export.fopen NOSTORE NONE
    "The application writes one file, the store, and only libs/core/src/store.cpp opens it"
    "fopen[(][^)]*\"[wa+]")

# --- which files ---------------------------------------------------------------

set(_code_extensions
    cpp hpp h cc cxx hxx mm m java kt swift qml js mjs html cmake in plist xml entitlements json)

# Third-party, generated or prose: not first-party source.
set(_skip_dirs .git deps build docs data tools .github .vscode)

file(GLOB _children LIST_DIRECTORIES true RELATIVE "${REPO_ROOT}" "${REPO_ROOT}/*")
set(_files "")
foreach(_child IN LISTS _children)
    if(_child IN_LIST _skip_dirs OR _child MATCHES "^[.]")
        continue()
    endif()
    if(IS_DIRECTORY "${REPO_ROOT}/${_child}")
        set(_patterns "")
        foreach(_extension IN LISTS _code_extensions)
            list(APPEND _patterns "${REPO_ROOT}/${_child}/*.${_extension}")
        endforeach()
        list(APPEND _patterns "${REPO_ROOT}/${_child}/CMakeLists.txt")
        file(GLOB_RECURSE _found RELATIVE "${REPO_ROOT}" ${_patterns})
        list(APPEND _files ${_found})
    endif()
endforeach()
file(GLOB _top RELATIVE "${REPO_ROOT}" "${REPO_ROOT}/CMakeLists.txt" "${REPO_ROOT}/CMakePresets.json"
    "${REPO_ROOT}/vcpkg.json")
list(APPEND _files ${_top})
list(REMOVE_DUPLICATES _files)

# The checker and its self-test name every forbidden thing: not scanned. Nor is
# apps/app/src/prototype/, which is a program of its own for trying the 3D board
# (docs/design/shake-prototype.md), never part of the application, and writes
# its frames to disk to look at them. Nor are the scanner's own privacy checker and
# its deliberate leak (libs/scan/tests): the checker names what the scanner must not do
# as patterns, as this one does, and the fixture is the leak it must catch.
list(FILTER _files EXCLUDE REGEX
    "^(tests/privacy|apps/app/src/prototype|libs/scan/tests/privacy_fixture)/|^libs/scan/tests/check_privacy[.]cmake$")

set(_store_source "libs/core/src/store.cpp")

# --- the scan ------------------------------------------------------------------

set(_findings "")
set(_scanned 0)
foreach(_file IN LISTS _files)
    math(EXPR _scanned "${_scanned} + 1")
    file(READ "${REPO_ROOT}/${_file}" _content)

    # Brackets and semicolons would be taken for list syntax when the file is
    # split into lines below.
    string(REPLACE ";" "<SEMI>" _content "${_content}")
    string(REPLACE "[" "<LB>" _content "${_content}")
    string(REPLACE "]" "<RB>" _content "${_content}")

    get_filename_component(_name "${_file}" NAME)
    get_filename_component(_extension "${_file}" LAST_EXT)
    if(_extension STREQUAL ".cmake" OR _name STREQUAL "CMakeLists.txt")
        set(_comment_line "^[ \t]*#")
    else()
        set(_comment_line "^[ \t]*(//|/[*]|[*])")
    endif()

    set(_lines "")
    set(_lines_ready FALSE)

    foreach(_index RANGE 1 ${_rule_count})
        set(_scope "${_rule_${_index}_scope}")
        # Production code is what the SRC and NOSTORE rules cover: libs/ and apps/,
        # not the tests and benchmarks beside it.
        set(_production FALSE)
        if(_file MATCHES "^(libs|apps)/" AND NOT _file MATCHES "/(tests|ui-tests|bench)/")
            set(_production TRUE)
        endif()
        if(_scope STREQUAL "SRC" AND NOT _production)
            continue()
        endif()
        if(_scope STREQUAL "NOSTORE" AND (NOT _production OR _file STREQUAL _store_source))
            continue()
        endif()
        if(_scope STREQUAL "CODE" AND (_extension MATCHES "^[.](cmake|json|xml|plist|in|entitlements)$" OR
                                       _name STREQUAL "CMakeLists.txt"))
            continue()
        endif()
        # The manifest rule is about manifests and the CMake and source files
        # that would name a permission; every other rule is about code.
        if(NOT _content MATCHES "${_rule_${_index}_regex}")
            continue()
        endif()

        if(NOT _lines_ready)
            string(REPLACE "\n" ";" _lines "${_content}")
            set(_lines_ready TRUE)
        endif()

        set(_number 0)
        foreach(_line IN LISTS _lines)
            math(EXPR _number "${_number} + 1")
            if(_line MATCHES "${_comment_line}")
                continue()
            endif()
            if(_line MATCHES "${_rule_${_index}_regex}")
                string(STRIP "${_line}" _shown)
                string(REPLACE "<SEMI>" ";" _shown "${_shown}")
                string(REPLACE "<LB>" "[" _shown "${_shown}")
                string(REPLACE "<RB>" "]" _shown "${_shown}")
                set(_findings
                    "${_findings}\n  ${_file}:${_number}: [${_rule_${_index}_id}] ${_rule_${_index}_advice}\n      ${_shown}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_scanned EQUAL 0)
    message(FATAL_ERROR "Privacy check found no source files under ${REPO_ROOT}.")
endif()

if(_findings)
    message(FATAL_ERROR "Privacy rules broken:${_findings}\n")
endif()

message(STATUS "Privacy: ${_scanned} files scanned, no rule broken")
