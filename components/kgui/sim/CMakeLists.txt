cmake_minimum_required(VERSION 3.18)
project(kgui_sim C)
include(${CMAKE_CURRENT_LIST_DIR}/../../../sdk/components/kui/sim/CMakeLists.txt)
file(GLOB KGUI_SCREENS ${CMAKE_CURRENT_LIST_DIR}/../screens/*.c)
kui_sim_add(kgui_sim SOURCES ${CMAKE_CURRENT_LIST_DIR}/kgui_sim.c ${KGUI_SCREENS}
                     INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../screens)
