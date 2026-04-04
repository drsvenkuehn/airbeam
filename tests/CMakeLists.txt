cmake_minimum_required(VERSION 3.20)

# GTest is already fetched by the parent CMakeLists.txt via FetchContent.
# This file registers all test executables with CTest.

# ── Unit tests ───────────────────────────────────────────────────────────────
set(UNIT_TEST_SOURCES
    unit/test_spsc_ring.cpp
    unit/test_alac_roundtrip.cpp
    unit/test_rtp_framing.cpp
    unit/test_aes_vectors.cpp
    unit/test_config.cpp
    unit/test_mdns_txt.cpp
    unit/test_volume_mapper.cpp
    unit/test_frame_accumulator.cpp
    unit/test_receiver_list.cpp
    unit/test_tray_menu.cpp
    unit/test_resampler.cpp
    unit/test_wasapi_stop_start.cpp
    unit/test_connection_controller.cpp
)

# ── Integration tests ────────────────────────────────────────────────────────
set(INTEGRATION_TEST_SOURCES
    integration/test_wasapi_correlation.cpp
    integration/test_raop_shairport.cpp
)

# ── E2E tests ────────────────────────────────────────────────────────────────
set(E2E_TEST_SOURCES
    e2e/test_1khz_sine.cpp
    e2e/test_stress_24h.cpp
)

# ── Core sources compiled into the test binary ───────────────────────────────
set(TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConnectionController.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StreamSession.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/VolumeMapper.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/AesCbcCipher.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/RetransmitBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/NtpClock.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/SdpBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/RsaKeyWrap.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/RaopSession.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/AlacEncoderThread.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/Resampler.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/WasapiCapture.cpp
    ${CMAKE_SOURCE_DIR}/src/discovery/TxtRecord.cpp
    ${CMAKE_SOURCE_DIR}/src/discovery/ReceiverList.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrayIcon.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TrayMenu.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/CustomPopup.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/BalloonNotify.cpp
    ${CMAKE_SOURCE_DIR}/src/localization/StringLoader.cpp
)

# ── AirBeamTests executable ──────────────────────────────────────────────────
add_executable(AirBeamTests
    ${UNIT_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
    ${E2E_TEST_SOURCES}
    ${TEST_CORE_SOURCES}
)

target_include_directories(AirBeamTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/
    ${CMAKE_BINARY_DIR}/generated/
)

target_compile_definitions(AirBeamTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    AIRBEAM_APPCAST_URL="${AIRBEAM_APPCAST_URL}"
)

target_link_libraries(AirBeamTests PRIVATE
    GTest::gtest_main
    alac_static
    nlohmann_json::nlohmann_json
    speexdsp_resampler
    ole32.lib
    oleaut32.lib
    avrt.lib
    bcrypt.lib
    winmm.lib
    ws2_32.lib
    user32.lib
    advapi32.lib
    shell32.lib   # Logger::OpenLogFolder → ShellExecuteW
    gdi32.lib
    comctl32.lib  # Common controls (TrayIcon, BalloonNotify)
    dwmapi.lib    # CustomPopup: DwmSetWindowAttribute
)

# ── CTest registration────────────────────────────────────────────────────────
include(GoogleTest)
gtest_discover_tests(AirBeamTests
    PROPERTIES LABELS "unit"
    DISCOVERY_TIMEOUT 30
)

# ── Icon asset validation ─────────────────────────────────────────────────────
# Verifies all 11 branded ICO files are present, have valid ICO headers,
# contain ≥ 2 frames, and are larger than a placeholder (> 1 KB).
# Fails the build if ICOs are accidentally reverted to single-frame stubs.
find_program(PWSH_EXECUTABLE NAMES pwsh pwsh.exe)
if(PWSH_EXECUTABLE)
    add_test(
        NAME    icon-validation
        COMMAND "${PWSH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/resources/icons/validate_icons.ps1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons/src"
    )
    set_tests_properties(icon-validation PROPERTIES LABELS "assets")
endif()

# ── Bonjour URL presence check ────────────────────────────────────────────────
# Verifies that all 7 locale RC files contain the Apple Bonjour download URL
# in the IDS_BALLOON_BONJOUR_MISSING string, and that resource_ids.h declares
# the BONJOUR_DOWNLOAD_URL constant.  Labeled "unit" so it runs in the default
# CI preset (-L unit).
if(PWSH_EXECUTABLE)
    add_test(
        NAME    bonjour-url-check
        COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
                "& { $url = 'https://support.apple.com/downloads/bonjour-for-windows'; \
$files = Get-ChildItem '${CMAKE_SOURCE_DIR}/resources/locales/*.rc'; \
$missing = $files | Where-Object { -not (Select-String -Quiet -SimpleMatch $url -Path $_.FullName) }; \
if ($missing) { Write-Error ('URL missing in: ' + ($missing.Name -join ', ')); exit 1 } \
if (-not (Select-String -Quiet -SimpleMatch 'BONJOUR_DOWNLOAD_URL' '${CMAKE_SOURCE_DIR}/resources/resource_ids.h')) { Write-Error 'BONJOUR_DOWNLOAD_URL missing from resource_ids.h'; exit 1 } \
Write-Host 'bonjour-url-check passed'; exit 0 }"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(bonjour-url-check PROPERTIES LABELS "unit")
endif()

# ── Bonjour fetch smoke test ──────────────────────────────────────────────────
# Runs fetch-bonjour.ps1 against a temp directory and verifies:
#   1. Exit code 0
#   2. BonjourPSSetup.exe is present in the temp dir
#   3. SHA-256 matches the pinned value in fetch-bonjour.ps1
# Labeled "unit" so it runs in the default CI preset (-L unit).
# Requires internet access on the build agent (standard on windows-latest runners).
if(PWSH_EXECUTABLE)
    add_test(
        NAME    bonjour-fetch-smoke
        COMMAND "${PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/installer/deps/fetch-bonjour.ps1"
                -OutputDir "${CMAKE_BINARY_DIR}/bonjour-fetch-smoke-tmp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(bonjour-fetch-smoke PROPERTIES
        LABELS "unit"
        PASS_REGULAR_EXPRESSION "hash-verified|Hash verified OK"
    )
endif()