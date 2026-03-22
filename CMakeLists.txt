cmake_minimum_required(VERSION 3.20)

project(AirBeam VERSION 1.0.0 LANGUAGES CXX C)

# ── C++17, MSVC hardening ───────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(
        /W4          # warning level 4
        /permissive- # strict conformance
        /WX          # warnings as errors
        /MP          # parallel compilation
        # Suppress spurious warnings from third-party headers
        /wd4100      # unreferenced formal parameter (ALAC headers)
        /wd4702      # unreachable code (ALAC headers)
    )
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0A00   # Windows 10+
    )
endif()

# ── Appcast URL ───────────────────────────────────────────────────────────────
# TODO(APPCAST_URL): Replace placeholder with real GitHub Pages URL before first release.
# Format: https://<org-or-user>.github.io/airbeam/appcast.xml
# GitHub Pages must be enabled on the repo and appcast.xml published there.
set(AIRBEAM_APPCAST_URL
    "https://TODO_ORG.github.io/airbeam/appcast.xml"
    CACHE STRING "WinSparkle appcast XML URL")

# ── FetchContent dependencies ───────────────────────────────────────────────
include(FetchContent)

# Apple ALAC reference encoder (Apache 2.0)
FetchContent_Declare(
    alac
    GIT_REPOSITORY https://github.com/macosforge/alac.git
    GIT_TAG        c38887c5c5e64a4b31108733bd79ca9b2496d987  # pinned 2016-05-11 (last commit on master; no tags exist on this repo — mirror: https://github.com/macosforge/alac)
    GIT_SHALLOW    TRUE
)

# nlohmann/json (MIT)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# Google Test (BSD-3-Clause)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json googletest)

# ALAC — build only the codec library
FetchContent_GetProperties(alac)
if(NOT alac_POPULATED)
    FetchContent_Populate(alac)
    add_library(alac_static STATIC
        ${alac_SOURCE_DIR}/codec/ALACBitUtilities.c
        ${alac_SOURCE_DIR}/codec/ALACDecoder.cpp
        ${alac_SOURCE_DIR}/codec/ALACEncoder.cpp
        ${alac_SOURCE_DIR}/codec/ag_dec.c
        ${alac_SOURCE_DIR}/codec/ag_enc.c
        ${alac_SOURCE_DIR}/codec/dp_dec.c
        ${alac_SOURCE_DIR}/codec/dp_enc.c
        ${alac_SOURCE_DIR}/codec/EndianPortable.c
        ${alac_SOURCE_DIR}/codec/matrix_dec.c
        ${alac_SOURCE_DIR}/codec/matrix_enc.c
    )
    target_include_directories(alac_static PUBLIC ${alac_SOURCE_DIR}/codec)
    target_compile_options(alac_static PRIVATE /W0)  # suppress ALAC warnings
    # ALAC's EndianPortable.c only checks __i386__, __x86_64__, TARGET_OS_WIN32 for
    # little-endian detection — none of which MSVC defines. Without this, the byte-swap
    # functions in ag_enc.c become no-ops, corrupting the entropy-coded bitstream.
    target_compile_definitions(alac_static PRIVATE TARGET_RT_LITTLE_ENDIAN=1)
endif()

# Optional resampler — libsamplerate (BSD-2-Clause)
option(AIRBEAM_USE_RESAMPLER "Build with libsamplerate resampler" ON)
if(AIRBEAM_USE_RESAMPLER)
    FetchContent_Declare(
        libsamplerate
        GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
        GIT_TAG        0.2.2
        GIT_SHALLOW    TRUE
    )
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)  # don't build samplerate tests
    FetchContent_MakeAvailable(libsamplerate)
    # Suppress warnings in vendored libsamplerate (C4127 constant conditional etc.)
    if(TARGET samplerate)
        target_compile_options(samplerate PRIVATE /W0)
    endif()
    add_compile_definitions(AIRBEAM_HAS_RESAMPLER)
endif()

# ── Version header (configure_file) ─────────────────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/resources/AirBeam.rc.in"
    "${CMAKE_BINARY_DIR}/generated/AirBeam.rc"
    @ONLY
)

# ── Source files ─────────────────────────────────────────────────────────────
set(AIRBEAM_SOURCES
    src/main.cpp

    src/core/AppController.cpp
    src/core/Config.cpp
    src/core/Logger.cpp

    src/audio/WasapiCapture.cpp
    src/audio/AlacEncoderThread.cpp
    src/audio/Resampler.cpp

    src/protocol/RaopSession.cpp
    src/protocol/SdpBuilder.cpp
    src/protocol/RsaKeyWrap.cpp
    src/protocol/AesCbcCipher.cpp
    src/protocol/RetransmitBuffer.cpp
    src/protocol/NtpClock.cpp
    src/protocol/VolumeMapper.cpp

    src/discovery/BonjourLoader.cpp
    src/discovery/MdnsDiscovery.cpp
    src/discovery/ReceiverList.cpp
    src/discovery/TxtRecord.cpp

    src/ui/TrayIcon.cpp
    src/ui/TrayMenu.cpp
    src/ui/VolumePopup.cpp
    src/ui/BalloonNotify.cpp
    src/ui/StartupRegistry.cpp

    src/update/SparkleIntegration.cpp

    src/localization/StringLoader.cpp
)

set(AIRBEAM_RESOURCES
    ${CMAKE_BINARY_DIR}/generated/AirBeam.rc
)

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(AirBeam WIN32
    ${AIRBEAM_SOURCES}
    ${AIRBEAM_RESOURCES}
)

target_include_directories(AirBeam PRIVATE
    src/
    ${CMAKE_BINARY_DIR}/generated/
    ${CMAKE_SOURCE_DIR}/resources/
)

target_compile_definitions(AirBeam PRIVATE
    AIRBEAM_APPCAST_URL="${AIRBEAM_APPCAST_URL}"
    AIRBEAM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AIRBEAM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AIRBEAM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(AirBeam PRIVATE
    alac_static
    nlohmann_json::nlohmann_json
    # Win32 APIs
    ole32.lib
    oleaut32.lib
    avrt.lib       # MMCSS (AvSetMmThreadCharacteristics)
    bcrypt.lib     # BCrypt AES + RSA
    winmm.lib
    shell32.lib
    comctl32.lib
    user32.lib
    gdi32.lib
    advapi32.lib
    ws2_32.lib     # Winsock
)

if(AIRBEAM_USE_RESAMPLER)
    target_link_libraries(AirBeam PRIVATE samplerate)
endif()

# ── Locale key-sync validation target ───────────────────────────────────────
find_program(POWERSHELL_EXE powershell.exe)
if(POWERSHELL_EXE)
    add_custom_target(check-locale-keys
        COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/.specify/scripts/powershell/check-locale-keys.ps1"
            -CanonicalFile "${CMAKE_SOURCE_DIR}/resources/locales/strings_en.rc"
            -LocaleDir "${CMAKE_SOURCE_DIR}/resources/locales"
        COMMENT "Checking IDS_* key coverage across all locale RC files"
        VERBATIM
    )
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
