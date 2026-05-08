include(GNUInstallDirs)
include(FindPackageHandleStandardArgs)

find_path(LIBUSB_INCLUDE_DIR
    NAMES libusb.h
    PATH_SUFFIXES libusb-1.0
)

find_library(LIBUSB_LIBRARY
    NAMES usb-1.0
)

set(LIBUSB_VERSION 1.0)

find_package_handle_standard_args(libusb
  REQUIRED_VARS
    LIBUSB_INCLUDE_DIR
    LIBUSB_LIBRARY
  VERSION_VAR LIBUSB_VERSION
)

if(LIBUSB_FOUND AND NOT TARGET libusb::usb-1.0)
    add_library(libusb::usb-1.0 UNKNOWN IMPORTED)
    set_target_properties(libusb::usb-1.0
      PROPERTIES
        IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}"
    )
endif()
