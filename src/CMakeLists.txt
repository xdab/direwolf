
# global includes
# not ideal but not so slow
# otherwise use target_include_directories
include_directories(
  ${HAMLIB_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${UDEV_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIRS}
  ${SNDIO_INCLUDE_DIRS}
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
  config.c
  demod_afsk.c
  demod.c
  dlq.c
  dsp.c
  dwsock.c
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
  kissnet.c
  multi_modem.c
  ptt.c
  recv.c
  rrbb.c
  tq.c
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
endif()

add_executable(direwolf
  ${direwolf_SOURCES}
  )

target_link_libraries(direwolf
  ${MISC_LIBRARIES}
  ${REGEX_LIBRARIES}
  ${HIDAPI_LIBRARIES}
  Threads::Threads
  ${HAMLIB_LIBRARIES}
  ${ALSA_LIBRARIES}
  ${UDEV_LIBRARIES}
  ${PORTAUDIO_LIBRARIES}
  ${SNDIO_LIBRARIES}
  )

if(WIN32 OR CYGWIN)
  set_target_properties(direwolf
    PROPERTIES COMPILE_FLAGS "-DUSE_REGEX_STATIC"
    )
  target_link_libraries(direwolf winmm ws2_32 setupapi)
endif()


# List USB audio adapters than can use GPIO for PTT.
# Originally for Linux only (using udev).
# Version 1.7 adds it for Windows.  Needs hidapi library.

# cm108
if(UDEV_FOUND OR WIN32 OR CYGWIN)
  list(APPEND cm108_SOURCES
    cm108.c
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
if(UDEV_FOUND OR WIN32 OR CYGWIN)
  install(TARGETS cm108 DESTINATION ${INSTALL_BIN_DIR})
endif()
