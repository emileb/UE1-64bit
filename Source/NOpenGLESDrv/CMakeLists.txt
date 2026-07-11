project(NOpenGLESDrv C CXX)

if(NOT USE_SDL)
  message(FATAL_ERROR "NOpenGLESDrv requires NSDLDrv to function.")
endif()

# UE1_SOURCE_DIR (from project(UE1)) instead of CMAKE_SOURCE_DIR so paths stay
# correct when built via add_subdirectory from a parent project (Android).
set(SRC_FILES "NOpenGLESDrv.cpp" "${UE1_SOURCE_DIR}/../Thirdparty/glad_es/glad.c")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${UE1_SOURCE_DIR}/../Thirdparty/glad_es
  ${UE1_SOURCE_DIR}/../Thirdparty
  ${SDL2_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})

target_link_libraries(${PROJECT_NAME} Render Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOPENGLESDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
