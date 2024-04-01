# This is a trick to avoid more complication
# because configure_file() is done at configuration time

set(CUSTOM_TEST_BINARY_DIR "${CMAKE_BINARY_DIR}/test")
set(GEN_PACKETS_BIN "${CMAKE_BINARY_DIR}/src/gen_packets${CMAKE_EXECUTABLE_SUFFIX}")
set(ATEST_BIN "${CMAKE_BINARY_DIR}/src/atest${CMAKE_EXECUTABLE_SUFFIX}")
set(FXSEND_BIN "${CMAKE_BINARY_DIR}/test/fxsend${CMAKE_EXECUTABLE_SUFFIX}")
set(FXREC_BIN "${CMAKE_BINARY_DIR}/test/fxrec${CMAKE_EXECUTABLE_SUFFIX}")

if(WIN32)
  set(CUSTOM_SCRIPT_SUFFIX ".bat")
else()
  set(CUSTOM_SCRIPT_SUFFIX "")
endif()

set(TEST_CHECK-FX25_FILE "check-fx25")
set(TEST_CHECK-MODEM1200_FILE "check-modem1200")
set(TEST_CHECK-MODEM300_FILE "check-modem300")

# generate the scripts that run the tests

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-FX25_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-FX25_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM1200_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_FILE}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

configure_file(
  "${CUSTOM_TEST_SCRIPTS_DIR}/${TEST_CHECK-MODEM300_FILE}"
  "${CUSTOM_TEST_BINARY_DIR}${CUSTOM_SCRIPT_SUFFIX}"
  @ONLY
  )

# global includes
# not ideal but not so slow
# otherwise use target_include_directories
include_directories(
  ${CUSTOM_SRC_DIR}
  ${HAMLIB_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${UDEV_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIRS}
  ${CUSTOM_GEOTRANZ_DIR}
  ${CMAKE_BINARY_DIR}/src
  )

if(WIN32 OR CYGWIN)
  include_directories(
    ${CUSTOM_REGEX_DIR}
  )
endif()


# Unit test for location coordinate conversion.
list(APPEND lltest_SOURCES
  ${CUSTOM_SRC_DIR}/latlong.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(lltest
  ${lltest_SOURCES}
  )

set_target_properties(lltest
  PROPERTIES COMPILE_FLAGS "-DLLTEST"
  )

target_link_libraries(lltest
  ${MISC_LIBRARIES}
  )


# Unit test for KISS encapsulation.
list(APPEND kisstest_SOURCES
  ${CUSTOM_SRC_DIR}/kiss_frame.c
  )

add_executable(kisstest
  ${kisstest_SOURCES}
  )

set_target_properties(kisstest
  PROPERTIES COMPILE_FLAGS "-DKISSTEST"
  )


# Unit test for constructing frames besides UI.
list(APPEND pad2test_SOURCES
  ${CUSTOM_SRC_DIR}/ax25_pad2.c
  ${CUSTOM_SRC_DIR}/ax25_pad.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(pad2test
  ${pad2test_SOURCES}
  )

set_target_properties(pad2test
  PROPERTIES COMPILE_FLAGS "-DPAD2TEST -DUSE_REGEX_STATIC"
  )

target_link_libraries(pad2test
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(pad2test ws2_32)
endif()


# Unit Test for XID frame encode/decode.
list(APPEND xidtest_SOURCES
  ${CUSTOM_SRC_DIR}/xid.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(xidtest
  ${xidtest_SOURCES}
  )

set_target_properties(xidtest
  PROPERTIES COMPILE_FLAGS "-DXIDTEST"
  )

target_link_libraries(xidtest
  ${MISC_LIBRARIES}
  )


# Unit Test FX.25 algorithm.

list(APPEND fxsend_SOURCES
  ${CUSTOM_SRC_DIR}/fx25_send.c
  ${CUSTOM_SRC_DIR}/fx25_encode.c
  ${CUSTOM_SRC_DIR}/fx25_init.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(fxsend
  ${fxsend_SOURCES}
  )

set_target_properties(fxsend
  PROPERTIES COMPILE_FLAGS "-DFXTEST"
  )

list(APPEND fxrec_SOURCES
  ${CUSTOM_SRC_DIR}/fx25_rec.c
  ${CUSTOM_SRC_DIR}/fx25_extract.c
  ${CUSTOM_SRC_DIR}/fx25_init.c
  ${CUSTOM_SRC_DIR}/fcs_calc.c
  ${CUSTOM_SRC_DIR}/textcolor.c
  )

add_executable(fxrec
  ${fxrec_SOURCES}
  )

set_target_properties(fxrec
  PROPERTIES COMPILE_FLAGS "-DFXTEST"
  )

# doing ctest on previous programs

add_test(lltest lltest)
add_test(kisstest kisstest)
add_test(pad2test pad2test)
add_test(xidtest xidtest)

add_test(check-fx25 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-FX25_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem1200 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM1200_FILE}${CUSTOM_SCRIPT_SUFFIX}")
add_test(check-modem300 "${CUSTOM_TEST_BINARY_DIR}/${TEST_CHECK-MODEM300_FILE}${CUSTOM_SCRIPT_SUFFIX}")


#  -----------------------------  Manual tests and experiments  ---------------------------
if(OPTIONAL_TEST)

  # For demodulator tweaking experiments.
  list(APPEND testagc_SOURCES
    ${CUSTOM_SRC_DIR}/atest.c
    ${CUSTOM_SRC_DIR}/demod.c
    ${CUSTOM_SRC_DIR}/dsp.c
    ${CUSTOM_SRC_DIR}/demod_afsk.c
    ${CUSTOM_SRC_DIR}/hdlc_rec.c
    ${CUSTOM_SRC_DIR}/hdlc_rec2.c
    ${CUSTOM_SRC_DIR}/multi_modem.c
    ${CUSTOM_SRC_DIR}/rrbb.c
    ${CUSTOM_SRC_DIR}/fcs_calc.c
    ${CUSTOM_SRC_DIR}/ax25_pad.c
    ${CUSTOM_SRC_DIR}/serial_port.c
    ${CUSTOM_SRC_DIR}/dtime_now.c
    ${CUSTOM_SRC_DIR}/latlong.c
    ${CUSTOM_SRC_DIR}/symbols.c
    ${CUSTOM_SRC_DIR}/textcolor.c
    )

  add_executable(testagc
    ${testagc_SOURCES}
    )

  target_link_libraries(testagc
    ${MISC_LIBRARIES}
    ${GPSD_LIBRARIES}
    Threads::Threads
    )

  if(WIN32 OR CYGWIN)
    target_link_libraries(testagc ws2_32)
  endif()

  endif()  # OPTIONAL_TEST
