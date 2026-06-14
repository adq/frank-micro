
# - Try to find libxcb-dri3
# Once done this will define
#
# XCB_DRI3_FOUND - system has libX11-xcb
# XCB_DRI3_LIBRARIES - Link these to use libX11-xcb
# XCB_DRI3_INCLUDE_DIR - the libX11-xcb include dir
# XCB_DRI3_DEFINITIONS - compiler switches required for using libX11-xcb

# Copyright (c) 2011 Fredrik Höglund <fredrik@kde.org>
# Copyright (c) 2008 Helio Chissini de Castro, <helio@kde.org>
# Copyright (c) 2007 Matthias Kretz, <kretz@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

IF (NOT WIN32)
    # use pkg-config to get the directories and then use these values
    # in the FIND_PATH() and FIND_LIBRARY() calls
    FIND_PACKAGE(PkgConfig)
    PKG_CHECK_MODULES(PKG_XCB_DRI3 QUIET xcb-dri3)

    SET(XCB_DRI3_DEFINITIONS ${PKG_XCB_DRI3_CFLAGS})

    FIND_PATH(XCB_DRI3_INCLUDE_DIR NAMES xcb/dri3.h HINTS ${PKG_XCB_DRI3_INCLUDE_DIRS})
    FIND_LIBRARY(XCB_DRI3_LIBRARIES NAMES xcb-dri3 HINTS ${PKG_XCB_DRI3_LIBRARY_DIRS})

    include(FindPackageHandleStandardArgs)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(XCB_DRI3 DEFAULT_MSG XCB_DRI3_LIBRARIES XCB_DRI3_INCLUDE_DIR)

    MARK_AS_ADVANCED(XCB_DRI3_INCLUDE_DIR XCB_DRI3_LIBRARIES)
ENDIF (NOT WIN32)