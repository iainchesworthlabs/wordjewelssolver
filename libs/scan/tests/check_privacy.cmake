# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The scanner touches a camera pointed at a child's room, so the privacy rules of
# the MVP (docs/design/application.md, "Privacy, built in and checked") are held in
# its code by a scan, in the way the platform-isolation check holds the rule on
# #ifdef:
#
#   cmake -DREPO_ROOT=<repository root> -P libs/scan/tests/check_privacy.cmake
#
#   - frames are not written to disk: nothing in the scanning library, the camera and
#     board_reader interfaces or their implementations includes a file or stream
#     header, uses a file class, or calls a file API; and no camera class is asked to
#     keep one (QImageCapture, captureToFile, QBuffer, a QImage or QPixmap save);
#   - no audio is captured: no audio-capture class, and no microphone permission;
#   - nothing is sent: no network class or socket header.
#
# It reads what the scanner is made of: everything under libs/scan, the camera and
# board_reader files of every platform (libs/platforms/interfaces/wjs/platform/camera.hpp,
# board_reader.hpp, and any camera_* or board_reader_* beside a platform's sources,
# which includes the iOS ones when they are written), and Mode A's camera source
# and its backends (apps/app/src/camera, apps/app/src/variants/camera-*), which is where the
# desktop's frames are taken. Lines that are // comments are skipped, so a comment
# can say what the code must not do.
#
# It is a pattern scan, not a parser: it holds the line the way
# check_platform_isolation.ps1 does, and review holds the rest.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

file(GLOB_RECURSE _scanner_sources
    "${REPO_ROOT}/libs/scan/*.cpp" "${REPO_ROOT}/libs/scan/*.hpp" "${REPO_ROOT}/libs/scan/*.mm")
file(GLOB_RECURSE _platform_sources
    "${REPO_ROOT}/libs/platforms/*/camera_*"
    "${REPO_ROOT}/libs/platforms/*/board_reader_*"
    "${REPO_ROOT}/libs/platforms/interfaces/wjs/platform/camera.hpp"
    "${REPO_ROOT}/libs/platforms/interfaces/wjs/platform/board_reader.hpp")
file(GLOB_RECURSE _app_camera_sources
    "${REPO_ROOT}/apps/app/src/camera/*"
    "${REPO_ROOT}/apps/app/src/variants/camera-*/*")
# The scanner's own tests (libs/scan/tests) are not what the check reads.
set(_scanner_production "")
foreach(_source IN LISTS _scanner_sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    if(NOT _relative MATCHES "^libs/scan/tests/")
        list(APPEND _scanner_production "${_source}")
    endif()
endforeach()
set(_scanner_sources ${_scanner_production})
set(_sources ${_scanner_sources} ${_platform_sources} ${_app_camera_sources})
list(REMOVE_DUPLICATES _sources)

# What each rule forbids: a name for the finding, and a regular expression (CMake's
# flavour) for the line.
set(_rules
    "file output|#[ \t]*include[ \t]*[<\"](fstream|cstdio|stdio[.]h|filesystem|fcntl[.]h|unistd[.]h|QFile|QSaveFile|QDir|QTemporaryFile|QImageWriter)[>\"]"
    "file output|std::(ofstream|ifstream|fstream|filesystem)"
    "file output|[^A-Za-z_](fopen|freopen|fwrite|fputs|fprintf|tmpfile|mkstemp|CreateFile[AW]?|NSFileManager|writeToFile|AVAssetWriter|AVCaptureMovieFileOutput|UIImageWriteToSavedPhotosAlbum|PHPhotoLibrary)[^A-Za-z_]"
    "file output|[^A-Za-z_](QFile|QSaveFile|QTemporaryFile|QImageWriter)[^A-Za-z_]"
    "frame capture|[^A-Za-z_](QImageCapture|QCameraImageCapture|captureToFile|QBuffer)[^A-Za-z_]"
    "frame capture|[^A-Za-z_](QImage|QPixmap)::save[^A-Za-z_]"
    "audio capture|[^A-Za-z_](QAudioSource|QAudioInput|QMediaRecorder|AVAudioRecorder|AVAudioEngine|AVCaptureAudioDataOutput|AVCaptureDeviceInput.*audio|AVMediaTypeAudio|NSMicrophoneUsageDescription)[^A-Za-z_]"
    "network|[^A-Za-z_](QNetworkAccessManager|QNetworkRequest|QTcpSocket|QUdpSocket|NSURLSession|NSURLConnection|curl_easy_init)[^A-Za-z_]"
    "network|#[ \t]*include[ \t]*<(sys/socket[.]h|netinet/[a-z.]+|arpa/inet[.]h|winsock2?[.]h|netdb[.]h)>")

set(_failures "")
set(_checked 0)
if(NOT _sources)
    # A root with nothing to read passes every rule, which is the wrong root and not a clean
    # scanner (libs/scan/tests/check_privacy_selftest.cmake holds this).
    message(FATAL_ERROR
        "No scanner sources found under ${REPO_ROOT}: libs/scan, the camera and board_reader "
        "files of libs/platforms and apps/app/src/camera are where the check looks.")
endif()
foreach(_source IN LISTS _sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    math(EXPR _checked "${_checked} + 1")
    file(STRINGS "${_source}" _lines)
    set(_number 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _number "${_number} + 1")
        if(_line MATCHES "^[ \t]*//")
            continue()
        endif()
        # Padded, so that a name at the start or end of a line has a character each side.
        set(_text " ${_line} ")
        foreach(_rule IN LISTS _rules)
            string(FIND "${_rule}" "|" _bar)
            string(SUBSTRING "${_rule}" 0 ${_bar} _what)
            math(EXPR _after "${_bar} + 1")
            string(SUBSTRING "${_rule}" ${_after} -1 _pattern)
            if(_text MATCHES "${_pattern}")
                set(_failures "${_failures}\n  ${_relative}:${_number}: ${_what}: ${_line}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_failures)
    message(FATAL_ERROR
        "The scanner may not write a frame to disk, capture audio or use the network "
        "(docs/design/application.md, \"Privacy, built in and checked\"):${_failures}")
endif()
message(STATUS "scan.privacy: ${_checked} scanner source(s) checked, no file output, audio capture or network")
