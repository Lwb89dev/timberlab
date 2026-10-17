cmake_minimum_required(VERSION 3.21)

project(TimberLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Vendored Qt and OCCT live in the repo working tree and are gitignored.
# An explicit CMAKE_PREFIX_PATH still wins because it is prepended by the user
# only when they pass -D; here we append the vendored prefixes when present.
set(_TIMBERLAB_QT "${CMAKE_CURRENT_SOURCE_DIR}/deps/qt/6.8.3/gcc_64")
set(_TIMBERLAB_OCCT "${CMAKE_CURRENT_SOURCE_DIR}/deps/occt-install")
# The vendored prefixes are the Linux desktop build. An Android (or other)
# configure must pass its own CMAKE_PREFIX_PATH. Linking gcc_64 into a
# tablet toolchain would succeed at configure time and fail on the device.
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
  if(EXISTS "${_TIMBERLAB_QT}/lib/cmake/Qt6/Qt6Config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${_TIMBERLAB_QT}")
  endif()
  if(EXISTS "${_TIMBERLAB_OCCT}/lib/cmake/opencascade/OpenCASCADEConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${_TIMBERLAB_OCCT}")
  endif()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL QuickControls2 LinguistTools)
qt_standard_project_setup()
find_package(OpenCASCADE REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

set(TIMBERLAB_MODEL_SOURCES
  src/units/units.cpp
  src/core/component.cpp
  src/core/joint.cpp
  src/core/document.cpp
  src/core/assembly.cpp
  src/constraints/dependency_graph.cpp
  src/constraints/binding.cpp
  src/geometry/solids.cpp
  src/materials/catalog.cpp
  src/storage/sha256.cpp
  src/storage/archive.cpp
  src/storage/preferences.cpp
  src/storage/library.cpp
  src/platform/paths.cpp
  third_party/miniz/miniz.c
  third_party/miniz/miniz_tdef.c
  third_party/miniz/miniz_tinfl.c
  third_party/miniz/miniz_zip.c
)

add_library(timberlab-model STATIC ${TIMBERLAB_MODEL_SOURCES})
set_target_properties(timberlab-model PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
target_include_directories(timberlab-model
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
)
target_include_directories(timberlab-model SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
target_compile_definitions(timberlab-model PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_definitions(timberlab-model PRIVATE _LARGEFILE64_SOURCE=1 __USE_LARGEFILE64)
target_link_libraries(timberlab-model
  PUBLIC
    ${OpenCASCADE_LIBRARIES}
    OpenSSL::Crypto
    SQLite::SQLite3
)
target_compile_options(timberlab-model PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
  $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
)

qt_add_executable(timberlab
  apps/timberlab/main.cpp
  apps/timberlab/self_check.cpp
  apps/timberlab/accept.cpp
  src/ui/DocumentController.cpp
  src/ui/ViewportItem.cpp
)
qt_add_resources(TIMBERLAB_RCC resources/timberlab.qrc)
target_sources(timberlab PRIVATE ${TIMBERLAB_RCC})
qt_add_translations(timberlab
  TS_FILES translations/timberlab_it.ts
  RESOURCE_PREFIX "/i18n"
)
target_include_directories(timberlab PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
  "${CMAKE_CURRENT_SOURCE_DIR}/apps/timberlab"
)
target_link_libraries(timberlab PRIVATE
  timberlab-model
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::OpenGL
  Qt6::QuickControls2
)
target_compile_options(timberlab PRIVATE -Wall -Wextra -Wpedantic)
# DT_RPATH, not DT_RUNPATH, and only for the GNU ld Linux build. OCCT's own
# libraries do not carry a run path, and RUNPATH is not used when resolving
# their dependencies. Android's lld does not take this flag.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_target_properties(timberlab PROPERTIES
    BUILD_RPATH "${OpenCASCADE_LIBRARY_DIR};${_TIMBERLAB_QT}/lib"
  )
  target_link_options(timberlab PRIVATE LINKER:--disable-new-dtags)
endif()

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.6.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

  add_executable(timberlab-tests
    tests/test_units.cpp
    tests/test_document.cpp
    tests/test_geometry.cpp
    tests/test_storage.cpp
    tests/test_catalog.cpp
    tests/test_constraints.cpp
    tests/test_joinery.cpp
    tests/test_propagation.cpp
    tests/test_assembly.cpp
    tests/test_library.cpp
  )
  target_link_libraries(timberlab-tests PRIVATE timberlab-model Catch2::Catch2WithMain)
  target_compile_definitions(timberlab-tests PRIVATE
    TIMBERLAB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TIMBERLAB_TEST_DIR="${CMAKE_BINARY_DIR}/testdata"
  )
  target_compile_options(timberlab-tests PRIVATE -Wall -Wextra -Wpedantic)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(timberlab-tests PRIVATE LINKER:--disable-new-dtags)
    set_target_properties(timberlab-tests PROPERTIES
      BUILD_RPATH "${OpenCASCADE_LIBRARY_DIR}"
    )
  endif()
  include(Catch)
  catch_discover_tests(timberlab-tests)
endif()
