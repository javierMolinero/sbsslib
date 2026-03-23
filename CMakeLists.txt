cmake_minimum_required(VERSION 3.16)
project(sbsslib VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SBSSLIB_BUILD_SHARED "Build sbsslib shared library" ON)
option(SBSSLIB_BUILD_STATIC "Build sbsslib static library" ON)
option(SBSSLIB_BUILD_CLI "Build sbssx executable" ON)
option(SBSSLIB_ENABLE_TESTS "Build unit tests" ON)
option(SBSSLIB_WITH_CFITSIO "Enable FITS reading through cfitsio" ON)

set(SBSS_MAX_IMAGE_PIXELS 16777216 CACHE STRING "Maximum number of FITS pixels supported by static workspace")
set(SBSS_MAX_DETECTIONS 50000 CACHE STRING "Maximum number of detections supported by static buffers")

add_compile_definitions(
    SBSS_MAX_IMAGE_PIXELS=${SBSS_MAX_IMAGE_PIXELS}
    SBSS_MAX_DETECTIONS=${SBSS_MAX_DETECTIONS}
)

set(SBSSLIB_PUBLIC_HEADERS
    include/sbsslib/sbsslib.h
)

set(SBSSLIB_SOURCES
    src/config.c
    src/detection.c
    src/fits_reader.c
    src/io_csv.c
)

set(SBSSLIB_LINK_LIBS "")

if(SBSSLIB_WITH_CFITSIO)
    find_package(PkgConfig QUIET)

    set(SBSSLIB_CFITSIO_HINTS
        ${PROJECT_SOURCE_DIR}/third_party/cfitsio_install
        $ENV{CFITSIO_ROOT}
    )

    find_path(CFITSIO_INCLUDE_DIR
        NAMES fitsio.h
        HINTS ${SBSSLIB_CFITSIO_HINTS}
        PATH_SUFFIXES include
    )

    find_library(CFITSIO_LIBRARY
        NAMES cfitsio
        HINTS ${SBSSLIB_CFITSIO_HINTS}
        PATH_SUFFIXES lib lib64
    )

    if((NOT CFITSIO_INCLUDE_DIR OR NOT CFITSIO_LIBRARY) AND PkgConfig_FOUND)
        pkg_check_modules(PC_CFITSIO QUIET cfitsio)
        if(PC_CFITSIO_FOUND)
            set(CFITSIO_INCLUDE_DIR ${PC_CFITSIO_INCLUDE_DIRS})
            set(CFITSIO_LIBRARY ${PC_CFITSIO_LINK_LIBRARIES})
        endif()
    endif()

    if(NOT CFITSIO_INCLUDE_DIR OR NOT CFITSIO_LIBRARY)
        message(FATAL_ERROR "CFITSIO not found. Install cfitsio or disable SBSSLIB_WITH_CFITSIO.")
    endif()

    list(APPEND SBSSLIB_LINK_LIBS ${CFITSIO_LIBRARY})

    set(SBSSLIB_ZLIB_HINTS
        ${PROJECT_SOURCE_DIR}/third_party/zlib_install
        $ENV{ZLIB_ROOT}
    )

    find_path(ZLIB_INCLUDE_DIR
        NAMES zlib.h
        HINTS ${SBSSLIB_ZLIB_HINTS}
        PATH_SUFFIXES include
    )

    find_library(ZLIB_LIBRARY
        NAMES z zlib libz libzs zlibstatic
        HINTS ${SBSSLIB_ZLIB_HINTS}
        PATH_SUFFIXES lib lib64
    )

    if(ZLIB_LIBRARY)
        list(APPEND SBSSLIB_LINK_LIBS ${ZLIB_LIBRARY})
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(SBSSLIB_BUILD_SHARED)
    add_library(sbsslib_shared SHARED ${SBSSLIB_SOURCES} ${SBSSLIB_PUBLIC_HEADERS})
    set_target_properties(sbsslib_shared PROPERTIES
        OUTPUT_NAME sbsslib
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
    )
    target_include_directories(sbsslib_shared
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${CFITSIO_INCLUDE_DIR}
    )
    target_link_libraries(sbsslib_shared PRIVATE ${SBSSLIB_LINK_LIBS})
    if(SBSSLIB_WITH_CFITSIO)
        target_compile_definitions(sbsslib_shared PUBLIC SBSSLIB_HAVE_CFITSIO=1)
    endif()
endif()

if(SBSSLIB_BUILD_STATIC)
    add_library(sbsslib_static STATIC ${SBSSLIB_SOURCES} ${SBSSLIB_PUBLIC_HEADERS})
    set_target_properties(sbsslib_static PROPERTIES OUTPUT_NAME sbsslib)
    target_include_directories(sbsslib_static
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${CFITSIO_INCLUDE_DIR}
    )
    target_link_libraries(sbsslib_static PRIVATE ${SBSSLIB_LINK_LIBS})
    if(SBSSLIB_WITH_CFITSIO)
        target_compile_definitions(sbsslib_static PUBLIC SBSSLIB_HAVE_CFITSIO=1)
    endif()
endif()

if(SBSSLIB_BUILD_CLI)
    add_executable(sbssx apps/sbssx.c)
    if(SBSSLIB_BUILD_SHARED)
        target_link_libraries(sbssx PRIVATE sbsslib_shared)
    elseif(SBSSLIB_BUILD_STATIC)
        target_link_libraries(sbssx PRIVATE sbsslib_static)
    else()
        message(FATAL_ERROR "SBSSLIB_BUILD_CLI requires SBSSLIB_BUILD_SHARED or SBSSLIB_BUILD_STATIC.")
    endif()
endif()

if(SBSSLIB_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)

if(SBSSLIB_BUILD_SHARED)
    install(TARGETS sbsslib_shared
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(SBSSLIB_BUILD_STATIC)
    install(TARGETS sbsslib_static
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

if(SBSSLIB_BUILD_CLI)
    install(TARGETS sbssx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES config/sbsslib.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/sbsslib)
