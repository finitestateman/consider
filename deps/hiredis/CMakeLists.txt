CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)

OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON)
OPTION(ENABLE_SSL "Build hisider_ssl for SSL support" OFF)
OPTION(DISABLE_TESTS "If tests should be compiled or not" OFF)
OPTION(ENABLE_SSL_TESTS "Should we test SSL connections" OFF)
OPTION(ENABLE_EXAMPLES "Enable building hisider examples" OFF)
OPTION(ENABLE_ASYNC_TESTS "Should we run all asynchronous API tests" OFF)

MACRO(getVersionBit name)
  SET(VERSION_REGEX "^#define ${name} (.+)$")
  FILE(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/hisider.h"
    VERSION_BIT REGEX ${VERSION_REGEX})
  STRING(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${VERSION_BIT}")
ENDMACRO(getVersionBit)

getVersionBit(HIREDIS_MAJOR)
getVersionBit(HIREDIS_MINOR)
getVersionBit(HIREDIS_PATCH)
getVersionBit(HIREDIS_SONAME)
SET(VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")
MESSAGE("Detected version: ${VERSION}")

PROJECT(hisider LANGUAGES "C" VERSION "${VERSION}")
INCLUDE(GNUInstallDirs)

# Hisider requires C99
SET(CMAKE_C_STANDARD 99)
SET(CMAKE_DEBUG_POSTFIX d)

SET(hisider_sources
    alloc.c
    async.c
    hisider.c
    net.c
    read.c
    sds.c
    sockcompat.c)

SET(hisider_sources ${hisider_sources})

IF(WIN32)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
ENDIF()

ADD_LIBRARY(hisider ${hisider_sources})
ADD_LIBRARY(hisider::hisider ALIAS hisider)
set(hisider_export_name hisider CACHE STRING "Name of the exported target")
set_target_properties(hisider PROPERTIES EXPORT_NAME ${hisider_export_name})

SET_TARGET_PROPERTIES(hisider
    PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    VERSION "${HIREDIS_SONAME}")
IF(MSVC)
    SET_TARGET_PROPERTIES(hisider
        PROPERTIES COMPILE_FLAGS /Z7)
ENDIF()
IF(WIN32)
    TARGET_LINK_LIBRARIES(hisider PUBLIC ws2_32 crypt32)
ELSEIF(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    TARGET_LINK_LIBRARIES(hisider PUBLIC m)
ELSEIF(CMAKE_SYSTEM_NAME MATCHES "SunOS")
    TARGET_LINK_LIBRARIES(hisider PUBLIC socket)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(hisider PUBLIC $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

CONFIGURE_FILE(hisider.pc.in hisider.pc @ONLY)

set(CPACK_PACKAGE_VENDOR "Sider")
set(CPACK_PACKAGE_DESCRIPTION "\
Hisider is a minimalistic C client library for the Sider database.

It is minimalistic because it just adds minimal support for the protocol, \
but at the same time it uses a high level printf-alike API in order to make \
it much higher level than otherwise suggested by its minimal code base and the \
lack of explicit bindings for every Sider command.

Apart from supporting sending commands and receiving replies, it comes with a \
reply parser that is decoupled from the I/O layer. It is a stream parser designed \
for easy reusability, which can for instance be used in higher level language bindings \
for efficient reply parsing.

Hisider only supports the binary-safe Sider protocol, so you can use it with any Sider \
version >= 1.2.0.

The library comes with multiple APIs. There is the synchronous API, the asynchronous API \
and the reply parsing API.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sider/hisider")
set(CPACK_PACKAGE_CONTACT "michael dot grunder at gmail dot com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

include(CPack)

INSTALL(TARGETS hisider
    EXPORT hisider-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

if (MSVC AND BUILD_SHARED_LIBS)
    INSTALL(FILES $<TARGET_PDB_FILE:hisider>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug RelWithDebInfo)
endif()

# For NuGet packages
INSTALL(FILES hisider.targets
    DESTINATION build/native)

INSTALL(FILES hisider.h read.h sds.h async.h alloc.h sockcompat.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hisider)

INSTALL(DIRECTORY adapters
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hisider)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hisider.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

export(EXPORT hisider-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/hisider-targets.cmake"
    NAMESPACE hisider::)

if(WIN32)
    SET(CMAKE_CONF_INSTALL_DIR share/hisider)
else()
    SET(CMAKE_CONF_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/hisider)
endif()
SET(INCLUDE_INSTALL_DIR include)
include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/hisider-config-version.cmake"
                                 COMPATIBILITY SameMajorVersion)
configure_package_config_file(hisider-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/hisider-config.cmake
                              INSTALL_DESTINATION ${CMAKE_CONF_INSTALL_DIR}
                              PATH_VARS INCLUDE_INSTALL_DIR)

INSTALL(EXPORT hisider-targets
        FILE hisider-targets.cmake
        NAMESPACE hisider::
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hisider-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/hisider-config-version.cmake
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})


IF(ENABLE_SSL)
    IF (NOT OPENSSL_ROOT_DIR)
        IF (APPLE)
            SET(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        ENDIF()
    ENDIF()
    FIND_PACKAGE(OpenSSL REQUIRED)
    SET(hisider_ssl_sources
        ssl.c)
    ADD_LIBRARY(hisider_ssl ${hisider_ssl_sources})
    ADD_LIBRARY(hisider::hisider_ssl ALIAS hisider_ssl)

    IF (APPLE AND BUILD_SHARED_LIBS)
        SET_PROPERTY(TARGET hisider_ssl PROPERTY LINK_FLAGS "-Wl,-undefined -Wl,dynamic_lookup")
    ENDIF()

    SET_TARGET_PROPERTIES(hisider_ssl
        PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        VERSION "${HIREDIS_SONAME}")
    IF(MSVC)
        SET_TARGET_PROPERTIES(hisider_ssl
            PROPERTIES COMPILE_FLAGS /Z7)
    ENDIF()
    TARGET_LINK_LIBRARIES(hisider_ssl PRIVATE OpenSSL::SSL)
    IF(WIN32)
        TARGET_LINK_LIBRARIES(hisider_ssl PRIVATE hisider)
    ENDIF()
    CONFIGURE_FILE(hisider_ssl.pc.in hisider_ssl.pc @ONLY)

    INSTALL(TARGETS hisider_ssl
        EXPORT hisider_ssl-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    if (MSVC AND BUILD_SHARED_LIBS)
        INSTALL(FILES $<TARGET_PDB_FILE:hisider_ssl>
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            CONFIGURATIONS Debug RelWithDebInfo)
    endif()

    INSTALL(FILES hisider_ssl.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hisider)

    INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hisider_ssl.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    export(EXPORT hisider_ssl-targets
           FILE "${CMAKE_CURRENT_BINARY_DIR}/hisider_ssl-targets.cmake"
           NAMESPACE hisider::)

    if(WIN32)
        SET(CMAKE_CONF_INSTALL_DIR share/hisider_ssl)
    else()
        SET(CMAKE_CONF_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/hisider_ssl)
    endif()
    configure_package_config_file(hisider_ssl-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/hisider_ssl-config.cmake
                                  INSTALL_DESTINATION ${CMAKE_CONF_INSTALL_DIR}
                                  PATH_VARS INCLUDE_INSTALL_DIR)

    INSTALL(EXPORT hisider_ssl-targets
        FILE hisider_ssl-targets.cmake
        NAMESPACE hisider::
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})

    INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hisider_ssl-config.cmake
        DESTINATION ${CMAKE_CONF_INSTALL_DIR})
ENDIF()

IF(NOT DISABLE_TESTS)
    ENABLE_TESTING()
    ADD_EXECUTABLE(hisider-test test.c)
    TARGET_LINK_LIBRARIES(hisider-test hisider)
    IF(ENABLE_SSL_TESTS)
        ADD_DEFINITIONS(-DHIREDIS_TEST_SSL=1)
        TARGET_LINK_LIBRARIES(hisider-test hisider_ssl)
    ENDIF()
    IF(ENABLE_ASYNC_TESTS)
        ADD_DEFINITIONS(-DHIREDIS_TEST_ASYNC=1)
        TARGET_LINK_LIBRARIES(hisider-test event)
    ENDIF()
    ADD_TEST(NAME hisider-test
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
ENDIF()

# Add examples
IF(ENABLE_EXAMPLES)
    ADD_SUBDIRECTORY(examples)
ENDIF(ENABLE_EXAMPLES)
