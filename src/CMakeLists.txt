
# global includes
# not ideal but not so slow
# otherwise use target_include_directories
include_directories(
  ${HAMLIB_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${UDEV_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIRS}
  ${SNDIO_INCLUDE_DIRS}
  ${CUSTOM_GEOTRANZ_DIR}
  ${CUSTOM_HIDAPI_DIR}
  )

if(WIN32 OR CYGWIN)
  include_directories(
    ${CUSTOM_REGEX_DIR}
  )
endif()


# direwolf
list(APPEND direwolf_SOURCES
  direwolf.c
  audio_stats.c
  ax25_pad.c
  ax25_pad2.c
  config.c
  demod_afsk.c
  demod.c
  dlq.c
  dsp.c
  dtime_now.c
  dwsock.c
  fcs_calc.c
  fcs_calc.c
  fx25_encode.c
  fx25_extract.c
  fx25_init.c
  fx25_rec.c
  fx25_send.c
  fx25_auto.c
  gen_tone.c
  hdlc_rec.c
  hdlc_rec2.c
  hdlc_send.c
  kiss_frame.c
  kiss.c
  kissserial.c
  kissnet.c
  latlong.c
  latlong.c
  log.c
  multi_modem.c
  serial_port.c
  ptt.c
  recv.c
  rrbb.c
  symbols.c
  textcolor.c
  tq.c
  xid.c
  xmit.c
  )

if(LINUX)
  list(APPEND direwolf_SOURCES
    audio.c
    )
  if(UDEV_FOUND)
    list(APPEND direwolf_SOURCES
      cm108.c
      )
  endif()
  if(AVAHI_CLIENT_FOUND)
    list(APPEND direwolf_SOURCES
      dns_sd_common.c
      dns_sd_avahi.c
      )
  endif()
  elseif(WIN32 OR CYGWIN) # windows
    list(APPEND direwolf_SOURCES
      audio_win.c
      cm108.c
      )
  elseif(HAVE_SNDIO)
    list(APPEND direwolf_SOURCES
      audio.c
      )
  else() # macOS freebsd
    list(APPEND direwolf_SOURCES
      audio_portaudio.c
      )
    if(USE_MACOS_DNSSD)
      list(APPEND direwolf_SOURCES
        dns_sd_common.c
        dns_sd_macos.c
        )
    endif()
endif()

add_executable(direwolf
  ${direwolf_SOURCES}
  )

target_link_libraries(direwolf
  ${GEOTRANZ_LIBRARIES}
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  ${HIDAPI_LIBRARIES}
  Threads::Threads
  ${HAMLIB_LIBRARIES}
  ${ALSA_LIBRARIES}
  ${UDEV_LIBRARIES}
  ${PORTAUDIO_LIBRARIES}
  ${SNDIO_LIBRARIES}
  ${AVAHI_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  set_target_properties(direwolf
    PROPERTIES COMPILE_FLAGS "-DUSE_REGEX_STATIC"
    )
  target_link_libraries(direwolf winmm ws2_32 setupapi)
endif()


# Convert between Latitude/Longitude and UTM coordinates.
# ll2utm
list(APPEND ll2utm_SOURCES
  ll2utm.c
  textcolor.c
  )

add_executable(ll2utm
  ${ll2utm_SOURCES}
  )

target_link_libraries(ll2utm
  ${GEOTRANZ_LIBRARIES}
  ${MISC_LIBRARIES}
  )

# utm2ll
list(APPEND utm2ll_SOURCES
  utm2ll.c
  textcolor.c
  )

add_executable(utm2ll
  ${utm2ll_SOURCES}
  )

target_link_libraries(utm2ll
  ${GEOTRANZ_LIBRARIES}
  ${MISC_LIBRARIES}
  )


# Test application to generate sound.
# gen_packets
list(APPEND gen_packets_SOURCES
  gen_packets.c
  ax25_pad.c
  ax25_pad2.c
  fx25_encode.c
  fx25_extract.c
  fx25_init.c
  fx25_send.c
  hdlc_send.c
  fcs_calc.c
  gen_tone.c
  textcolor.c
  dsp.c
  )

add_executable(gen_packets
  ${gen_packets_SOURCES}
  )

target_link_libraries(gen_packets
  ${MISC_LIBRARIES}
  )


# Unit test for AFSK demodulator
# atest
list(APPEND atest_SOURCES
  atest.c
  demod.c
  demod_afsk.c
  dsp.c
  fx25_extract.c
  fx25_encode.c
  fx25_init.c
  fx25_rec.c
  hdlc_rec.c
  hdlc_rec2.c
  multi_modem.c
  rrbb.c
  fcs_calc.c
  ax25_pad.c
  ax25_pad2.c
  serial_port.c
  dtime_now.c
  latlong.c
  symbols.c
  textcolor.c
  )

add_executable(atest
  ${atest_SOURCES}
  )

target_link_libraries(atest
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  set_target_properties(atest
    PROPERTIES COMPILE_FLAGS "-DUSE_REGEX_STATIC"
    )
endif()


# Multiple AGWPE network or serial port clients to test TNCs side by side.
# aclients
list(APPEND aclients_SOURCES
  aclients.c
  ax25_pad.c
  fcs_calc.c
  textcolor.c
  )

add_executable(aclients
  ${aclients_SOURCES}
  )

target_link_libraries(aclients
  ${MISC_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(aclients ws2_32)
endif()


# Talk to a KISS TNC.
# Note:  kiss_frame.c has conditional compilation on KISSUTIL.
# kissutil
list(APPEND kissutil_SOURCES
  kissutil.c
  kiss_frame.c
  ax25_pad.c
  fcs_calc.c
  textcolor.c
  serial_port.c
  dtime_now.c
  dwsock.c
  )

add_executable(kissutil
  ${kissutil_SOURCES}
  )

set_target_properties(kissutil
  PROPERTIES COMPILE_FLAGS "-DKISSUTIL"
  )

target_link_libraries(kissutil
  ${MISC_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(kissutil ws2_32)
endif()


# TNC interoperability testing for AX.25 connected mode.
# tnctest
list(APPEND tnctest_SOURCES
  tnctest.c
  textcolor.c
  dtime_now.c
  serial_port.c
  )

add_executable(tnctest
  ${tnctest_SOURCES}
  )

target_link_libraries(tnctest
  ${MISC_LIBRARIES}
  Threads::Threads
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(tnctest ws2_32)
endif()


# List USB audio adapters than can use GPIO for PTT.
# Originally for Linux only (using udev).
# Version 1.7 adds it for Windows.  Needs hidapi library.

# cm108
if(UDEV_FOUND OR WIN32 OR CYGWIN)
  list(APPEND cm108_SOURCES
    cm108.c
    textcolor.c
    )

  add_executable(cm108
    ${cm108_SOURCES}
    )

  set_target_properties(cm108
    PROPERTIES COMPILE_FLAGS "-DCM108_MAIN"
    )

  target_link_libraries(cm108
    ${MISC_LIBRARIES}
    )

  if (LINUX)
    target_link_libraries(cm108
      ${UDEV_LIBRARIES}
      )
  endif()

  if (WIN32 OR CYGWIN)
    target_link_libraries(cm108
      ${HIDAPI_LIBRARIES}
      ws2_32
      setupapi
      )
  endif()
endif()


install(TARGETS direwolf DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS ll2utm DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS utm2ll DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS aclients DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS gen_packets DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS atest DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS kissutil DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS tnctest DESTINATION ${INSTALL_BIN_DIR})
if(UDEV_FOUND OR WIN32 OR CYGWIN)
  install(TARGETS cm108 DESTINATION ${INSTALL_BIN_DIR})
endif()
