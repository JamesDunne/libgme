# Rules for building the demo.  Doesn't use an installed gme if you've already
# installed the project so if you're copying these rules you probably don't
# even need these next two lines if you're building against system-installed
# gme.
include_directories(${CMAKE_SOURCE_DIR}/gme ${CMAKE_SOURCE_DIR})
link_directories(${CMAKE_BINARY_DIR}/gme)

add_executable(nsf2midi Wave_Writer.cpp nsf2midi.cpp)

# Add command to copy build file over.
add_custom_command(TARGET demo
    POST_BUILD
    COMMAND cmake -E copy "${CMAKE_SOURCE_DIR}/test.nsf" ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Add convenience copy of test.nsf file for demo application"
    VERBATIM) # VERBATIM is essentially required, "please use correct command line kthx"

target_link_libraries(nsf2midi gme)
