#
# Find the opus include file and library
#
#  OPUS_FOUND - system has opus
#  OPUS_INCLUDE_DIRS - the opus include directory
#  OPUS_LIBRARIES - The libraries needed to use opus

find_path(OPUS_INCLUDE_DIR opus.h
    HINTS
    ${OPUS}
    PATH_SUFFIXES include/opus include
)

find_library(OPUS_LIBRARY
     NAMES opus
     HINTS
     ${OPUS}
     PATH_SUFFIXES lib
)

if(OPUS_LIBRARY)
    find_library(LIBM NAMES m)
    if(LIBM)
        list(APPEND OPUS_LIBRARY ${LIBM})
    endif()
endif()

# --

set(OPUS_LIBRARIES ${OPUS_LIBRARY})
set(OPUS_INCLUDE_DIRS ${OPUS_INCLUDE_DIR})

find_package_handle_standard_args(Opus
    DEFAULT_MSG
    OPUS_INCLUDE_DIRS OPUS_LIBRARIES
)

mark_as_advanced(OPUS_INCLUDE_DIR OPUS_LIBRARY)
