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
# Only the translation units directly exercised by current unit tests are
# included here; the rest are added as their own tests are implemented.
set(TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/VolumeMapper.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/AesCbcCipher.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/RetransmitBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/discovery/TxtRecord.cpp
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
    ole32.lib
    avrt.lib
    bcrypt.lib
    ws2_32.lib
    user32.lib
    advapi32.lib
    shell32.lib   # Logger::OpenLogFolder → ShellExecuteW
)

if(AIRBEAM_USE_RESAMPLER)
    target_link_libraries(AirBeamTests PRIVATE samplerate)
endif()

# ── CTest registration ────────────────────────────────────────────────────────
include(GoogleTest)
gtest_discover_tests(AirBeamTests
    PROPERTIES LABELS "unit"
    DISCOVERY_TIMEOUT 30
)
