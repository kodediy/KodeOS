cmake_minimum_required(VERSION 3.22)

# The SDK's components; components/ here holds only kgui.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/sdk/components")

# The board's settings first, then kodeOS's own.
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdk/components/kbsp/sdkconfig.board;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

# kodeOS's version; `-DPROJECT_VER=...` on the command line overrides it.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()

# The chip, set before project() or ESP-IDF guesses esp32 and caches it.
set(IDF_TARGET esp32p4)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(kodeos)

# The FAT image: Kody's pack in kode/ and storage_image/, which holds the C5
# image of this release (tools/build_c5.sh); its stamp goes to the build dir.
# Not flashed by `idf.py flash`; `idf.py storage-flash` writes it.
set(storage_stage "${CMAKE_BINARY_DIR}/storage_stage")
file(REMOVE_RECURSE "${storage_stage}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/sdk/components/kui/kody/pack/" DESTINATION "${storage_stage}/kode")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/storage_image")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/storage_image/" DESTINATION "${storage_stage}"
         PATTERN "README.md" EXCLUDE PATTERN "*.commit" EXCLUDE)
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/storage_image/c5_slave.commit")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/storage_image/c5_slave.commit" DESTINATION "${CMAKE_BINARY_DIR}")
endif()
# Staged at configure time, so a changed pack or image file configures again.
file(GLOB_RECURSE storage_inputs CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/sdk/components/kui/kody/pack/*"
     "${CMAKE_CURRENT_LIST_DIR}/storage_image/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storage_inputs})
fatfs_create_spiflash_image(storage "${storage_stage}")
