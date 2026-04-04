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
        /EHsc        # standard C++ exception handling (required; not in ci preset CXX_FLAGS_DEBUG)
        # Suppress spurious warnings from third-party headers
        /wd4100      # unreferenced formal parameter (ALAC headers)
        /wd4702      # unreachable code (ALAC headers)
        /wd4530      # C++ exception handler used, but unwind semantics not enabled (googletest)
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
set(AIRBEAM_APPCAST_URL
    "https://drsvenkuehn.github.io/airbeam/appcast.xml"
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

# libsodium (ISC) — crypto primitives for HAP pairing (Ed25519, X25519, ChaCha20-Poly1305)
FetchContent_Declare(
    libsodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG        1.0.20
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json googletest)

# ── OpenSSL 3.x (Apache-2.0) — required by csrp SRP-6a ─────────────────────
find_package(OpenSSL REQUIRED)

# ── libsodium — build static library from FetchContent ──────────────────────
FetchContent_GetProperties(libsodium)
if(NOT libsodium_POPULATED)
    FetchContent_Populate(libsodium)
endif()

# libsodium ships a CMakeLists.txt only in the cmake-build branch.
# We build the amalgamation manually for maximum portability.
file(GLOB_RECURSE LIBSODIUM_SOURCES
    "${libsodium_SOURCE_DIR}/src/libsodium/*.c"
)

# Remove assembly files (not needed for x64 MSVC — C intrinsics suffice)
list(FILTER LIBSODIUM_SOURCES EXCLUDE REGEX "\\.S$|\\.asm$")

add_library(sodium STATIC ${LIBSODIUM_SOURCES})
target_include_directories(sodium PUBLIC
    "${libsodium_SOURCE_DIR}/src/libsodium/include"
    "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium"
)
# Generate sodium/version.h from template if needed
if(EXISTS "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in")
    configure_file(
        "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in"
        "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h"
        @ONLY
    )
endif()
target_compile_definitions(sodium PUBLIC
    SODIUM_STATIC
    SODIUM_EXPORT=
)
target_compile_definitions(sodium PRIVATE
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(sodium PRIVATE /W0)  # suppress libsodium warnings

# ── csrp — vendored SRP-6a (BSD-2-Clause) ───────────────────────────────────
add_library(csrp STATIC
    third_party/csrp/srp.c
)
target_include_directories(csrp PUBLIC
    third_party/csrp/
)
target_link_libraries(csrp PUBLIC OpenSSL::Crypto)
target_compile_options(csrp PRIVATE /W0)

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

# speexdsp resampler — vendored BSD-3-Clause
add_subdirectory(third_party/speexdsp)

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
    src/core/ConnectionController.cpp
    src/core/StreamSession.cpp

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
    src/ui/CustomPopup.cpp
    src/ui/VolumePopup.cpp
    src/ui/BalloonNotify.cpp
    src/ui/StartupRegistry.cpp

    src/update/SparkleIntegration.cpp

    src/localization/StringLoader.cpp

    # AirPlay 2 module (Feature 010)
    src/airplay2/CredentialStore.cpp
    src/airplay2/HapPairing.cpp
    src/airplay2/AesGcmCipher.cpp
    src/airplay2/PtpClock.cpp
    src/airplay2/AirPlay2Session.cpp
    src/ui/PinDialog.cpp
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
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${CMAKE_SOURCE_DIR}/third_party/csrp/
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
    speexdsp_resampler
    sodium
    csrp
    OpenSSL::Crypto
    # Win32 APIs
    ole32.lib
    oleaut32.lib
    avrt.lib       # MMCSS (AvSetMmThreadCharacteristics)
    bcrypt.lib     # BCrypt AES + RSA
    winmm.lib
    shell32.lib
    comctl32.lib
    uxtheme.lib
    dwmapi.lib
    user32.lib
    gdi32.lib
    advapi32.lib
    ws2_32.lib     # Winsock
    winhttp.lib    # WinHTTP (AirPlay 2 HTTP/2 control)
    crypt32.lib    # CryptProtectData for CredentialStore
)

if(MSVC)
    # Suppress linker manifest — we embed our own via mt.exe POST_BUILD
    set_target_properties(AirBeam PROPERTIES
        LINK_FLAGS "/MANIFEST:NO"
    )

    # mt.exe lives beside rc.exe in the Windows Kit bin directory
    get_filename_component(MT_TOOL "${CMAKE_RC_COMPILER}" DIRECTORY)
    set(MT_TOOL "${MT_TOOL}/mt.exe")

    if(EXISTS "${MT_TOOL}")
        add_custom_command(TARGET AirBeam POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-DMT_TOOL=${MT_TOOL}"
                    "-DMANIFEST_FILE=${CMAKE_SOURCE_DIR}/resources/AirBeam.manifest"
                    "-DTARGET_EXE=$<TARGET_FILE:AirBeam>"
                    -P "${CMAKE_SOURCE_DIR}/cmake/EmbedManifest.cmake"
            COMMENT "Embedding application manifest (Common Controls v6 / PerMonitorV2)"
            VERBATIM
        )
    else()
        message(WARNING "mt.exe not found at ${MT_TOOL} — manifest will not be embedded")
    endif()
endif()

# ── Bonjour Print Services MSI— fetch and verify at build time ──────────────
# Downloads BonjourPSSetup.exe from the pinned Apple CDN URL, verifies SHA-256,
# and caches it at installer/deps/BonjourPSSetup.exe (gitignored).
# Required by the NSIS installer script at makensis time.
find_program(POWERSHELL_PROG NAMES pwsh powershell REQUIRED
    DOC "PowerShell 7+ or Windows PowerShell executable")

add_custom_command(
    OUTPUT  "${CMAKE_SOURCE_DIR}/installer/deps/BonjourPSSetup.exe"
    COMMAND "${POWERSHELL_PROG}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/installer/deps/fetch-bonjour.ps1"
            -OutputDir "${CMAKE_SOURCE_DIR}/installer/deps"
    COMMENT "Fetching and verifying Bonjour Print Services MSI"
    VERBATIM
)

add_custom_target(fetch-bonjour ALL
    DEPENDS "${CMAKE_SOURCE_DIR}/installer/deps/BonjourPSSetup.exe"
    COMMENT "Bonjour MSI ready"
)

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
