# global includes
# not ideal but not so slow
# otherwise use target_include_directories
include_directories(
  ${GPSD_INCLUDE_DIRS}
  ${HAMLIB_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${UDEV_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIRS}
  ${CUSTOM_GEOTRANZ_DIR}
  )

if(WIN32 OR CYGWIN)
  include_directories(
    ${CUSTOM_REGEX_DIR}
  )
endif()

# build gen_fff to create fsk_fast_filter.h
# optimization for slow processors
list(APPEND gen_fff_SOURCES
  demod_afsk.c
  dsp.c
  textcolor.c
  )

add_executable(gen_fff
  ${gen_fff_SOURCES}
  )

set_target_properties(gen_fff
  PROPERTIES COMPILE_FLAGS "-DGEN_FFF"
  )

add_custom_command(TARGET gen_fff
  POST_BUILD
  COMMAND gen_fff > fsk_fast_filter.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )


# direwolf
list(APPEND direwolf_SOURCES
  direwolf.c
  aprs_tt.c
  audio_stats.c
  ax25_link.c
  ax25_pad.c
  ax25_pad2.c
  beacon.c
  config.c
  decode_aprs.c
  dedupe.c
  demod_9600.c
  demod_afsk.c
  demod_psk.c
  demod.c
  digipeater.c
  cdigipeater.c
  dlq.c
  dsp.c
  dtime_now.c
  dtmf.c
  dwgps.c
  encode_aprs.c
  encode_aprs.c
  fcs_calc.c
  fcs_calc.c
  gen_tone.c
  hdlc_rec.c
  hdlc_rec2.c
  hdlc_send.c
  igate.c
  kiss_frame.c
  kiss.c
  kissserial.c
  kissnet.c
  latlong.c
  latlong.c
  log.c
  morse.c
  multi_modem.c
  waypoint.c
  serial_port.c
  pfilter.c
  ptt.c
  rdq.c
  recv.c
  rrbb.c
  server.c
  symbols.c
  telemetry.c
  textcolor.c
  tq.c
  tt_text.c
  tt_user.c
  xid.c
  xmit.c
  dwgps.c
  dwgpsnmea.c
  dwgpsd.c
  mheard.c
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
  elseif(WIN32 OR CYGWIN) # windows
    list(APPEND direwolf_SOURCES
      audio_win.c

      # icon
      # require plain gcc binary or link
      #${CMAKE_SOURCE_DIR}/cmake/cpack/direwolf.rc
      )
    list(REMOVE_ITEM direwolf_SOURCES
      dwgpsd.c
      )
  else() # macOS freebsd openbsd
    list(APPEND direwolf_SOURCES
      audio_portaudio.c
      )
endif()

add_executable(direwolf
  ${direwolf_SOURCES}
  )

add_dependencies(direwolf gen_fff)

target_link_libraries(direwolf
  ${GEOTRANZ_LIBRARIES}
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  Threads::Threads
  ${GPSD_LIBRARIES}
  ${HAMLIB_LIBRARIES}
  ${ALSA_LIBRARIES}
  ${UDEV_LIBRARIES}
  ${PORTAUDIO_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  set_target_properties(direwolf
    PROPERTIES COMPILE_FLAGS "-DUSE_REGEX_STATIC"
    )
  target_link_libraries(direwolf winmm ws2_32)
endif()

# decode_aprs
list(APPEND decode_aprs_SOURCES
  decode_aprs.c
  kiss_frame.c
  ax25_pad.c
  dwgpsnmea.c
  dwgps.c
  dwgpsd.c
  serial_port.c
  symbols.c
  textcolor.c
  fcs_calc.c
  latlong.c
  log.c
  telemetry.c
  tt_text.c
  )

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM decode_aprs_SOURCES
    dwgpsd.c
    )
endif()

add_executable(decode_aprs
  ${decode_aprs_SOURCES}
  )

add_dependencies(decode_aprs gen_fff)

set_target_properties(decode_aprs
  PROPERTIES COMPILE_FLAGS "-DDECAMAIN -DUSE_REGEX_STATIC"
  )

target_link_libraries(decode_aprs
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  Threads::Threads
  ${GPSD_LIBRARIES}
  )


# Convert between text and touch tone representation.
# text2tt
list(APPEND text2tt_SOURCES
  tt_text.c
  )

add_executable(text2tt
  ${text2tt_SOURCES}
  )

set_target_properties(text2tt
  PROPERTIES COMPILE_FLAGS "-DENC_MAIN"
  )

target_link_libraries(text2tt
  ${MISC_LIBRARIES}
  )

# tt2text
list(APPEND tt2text_SOURCES
  tt_text.c
  )

add_executable(tt2text
  ${tt2text_SOURCES}
  )

set_target_properties(tt2text
  PROPERTIES COMPILE_FLAGS "-DDEC_MAIN"
  )

target_link_libraries(tt2text
  ${MISC_LIBRARIES}
  )


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


# Convert from log file to GPX.
# log2gpx
list(APPEND log2gpx_SOURCES
  log2gpx.c
  textcolor.c
  )

add_executable(log2gpx
  ${log2gpx_SOURCES}
  )

target_link_libraries(log2gpx
  ${MISC_LIBRARIES}
  )


# Test application to generate sound.
# gen_packets
list(APPEND gen_packets_SOURCES
  gen_packets.c
  ax25_pad.c
  hdlc_send.c
  fcs_calc.c
  gen_tone.c
  morse.c
  dtmf.c
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
  demod_psk.c
  demod_9600.c
  dsp.c
  hdlc_rec.c
  hdlc_rec2.c
  multi_modem.c
  rrbb.c
  fcs_calc.c
  ax25_pad.c
  decode_aprs.c
  dwgpsnmea.c
  dwgps.c
  dwgpsd.c
  serial_port.c
  telemetry.c
  dtime_now.c
  latlong.c
  symbols.c
  tt_text.c
  textcolor.c
  )

if(WIN32 OR CYGWIN)
  list(REMOVE_ITEM atest_SOURCES
    dwgpsd.c
    )
endif()

add_executable(atest
  ${atest_SOURCES}
  )

add_dependencies(atest gen_fff)

target_link_libraries(atest
  ${MISC_LIBRARIES}
  ${GPSD_LIBRARIES}
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


# List USB audio adapters than can use GPIO for PTT.
# cm108
if(UDEV_FOUND)
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
    ${UDEV_LIBRARIES}
    )
endif()


# Touch Tone to Speech sample application.
# ttcalc
list(APPEND ttcalc_SOURCES
  ttcalc.c
  ax25_pad.c
  fcs_calc.c
  textcolor.c
  )

add_executable(ttcalc
  ${ttcalc_SOURCES}
  )

target_link_libraries(ttcalc
  ${MISC_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  target_link_libraries(ttcalc ws2_32)
endif()

install(TARGETS direwolf DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS decode_aprs DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS text2tt DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS tt2text DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS ll2utm DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS utm2ll DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS aclients DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS log2gpx DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS gen_packets DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS atest DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS ttcalc DESTINATION ${INSTALL_BIN_DIR})
install(TARGETS kissutil DESTINATION ${INSTALL_BIN_DIR})
if(UDEV_FOUND)
  install(TARGETS cm108 DESTINATION ${INSTALL_BIN_DIR})
endif()