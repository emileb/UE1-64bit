project(Unreal)

if(USE_SDL)
  set(SRC_FILES "Src/SDLLaunch.cpp")
else()
  set(SRC_FILES "Src/WinLaunch.cpp")
endif()

if(VITA)
  set(VITA_APP_NAME "Unreal")
  set(VITA_TITLEID  "UNRL00001")
  set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
  include("${VITASDK}/share/vita.cmake" REQUIRED)
endif()

if(TARGET_IS_WINDOWS)
  list(APPEND SRC_FILES "Src/Res/LaunchRes.rc")
  add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES})
elseif(ANDROID)
  # The app loads the game as a shared library and calls main() via the JNI glue.
  add_library(${PROJECT_NAME} SHARED ${SRC_FILES})
else()
  add_executable(${PROJECT_NAME} ${SRC_FILES})
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

if(USE_SDL)
  if(ANDROID)
    # NSDLDrv is pulled in whole-archive by the static link below; naming it
    # here as well makes lld see every symbol twice.
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})
  else()
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} NSDLDrv)
  endif()
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
elseif(TARGET_IS_WINDOWS)
  target_link_libraries(${PROJECT_NAME} Window)
else()
  message(FATAL_ERROR "Non-Windows platforms can only use NSDLDrv.")
endif()

if(BUILD_STATIC)
  set(LINK_PACKAGES ${INSTALL_TARGETS})
  list(REMOVE_ITEM LINK_PACKAGES ${PROJECT_NAME})
  target_link_libraries(${PROJECT_NAME} -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive)
else()
  target_link_libraries(${PROJECT_NAME} Engine Core)
endif()

if(ANDROID)
  # Package as libunreal.so (the parent scope clears the "lib" prefix).
  set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "lib" OUTPUT_NAME "unreal")

  # ENGINE_NAME names the touch-control layout XML files.
  target_compile_definitions(${PROJECT_NAME} PRIVATE ENGINE_NAME="unreal")

  # OpenTouch glue: JNI bridge + touch interface from Clibs_OpenTouch, compiled
  # into the engine .so. Phase 1 uses the shared alpha/ TouchInterface; the
  # engine gets its own mobile/touch_interface copy in Phase 2.
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_IMAGE_ANDROID_INCLUDE_DIR}
    ${CLIBS_DIR}
    ${CLIBS_DIR}/alpha
    ${TOUCH_CONTROLS_DIR}
  )
  target_sources(${PROJECT_NAME} PRIVATE
    "${UE1_SOURCE_DIR}/../mobile/game_interface.cpp"
    "${CLIBS_DIR}/sigma/android_jni.cpp"
    "${CLIBS_DIR}/alpha/touch_interface.cpp"
    "${CLIBS_DIR}/touch_interface_base.cpp"
    "${CLIBS_DIR}/LogWritter.c"
    "${CLIBS_DIR}/fts.c"
    "${CLIBS_DIR}/rand.c"
  )
  target_link_libraries(${PROJECT_NAME} SDL2_image touchcontrols saffal log)
endif()

if(VITA)
  target_link_libraries(${PROJECT_NAME}
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    openal
    xmp
    vitaGL
    vitashark
    mathneon
    SceShaccCgExt
    taihen_stub
    SceNet_stub
    SceNetCtl_stub
    SceAppMgr_stub
    SceAppUtil_stub
    SceAudio_stub
    SceAudioIn_stub
    SceCtrl_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceFios2_stub
    SceGxm_stub
    SceIme_stub
    SceShaccCg_stub
    SceSysmodule_stub
    ScePower_stub
    SceTouch_stub
    SceMotion_stub
    SceHid_stub
    SceVshBridge_stub
    SceSblSsMgr_stub
    SceKernelDmacMgr_stub
  )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(VITA)
  vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_CURRENT_SOURCE_DIR}/Res/sce_sys sce_sys
  )
endif()
