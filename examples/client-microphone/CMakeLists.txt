cmake_minimum_required(VERSION 3.12)

if (WHISPER_SDL2)
    set(TARGET whisper-client-microphone)
    add_executable(${TARGET} client-microphone.cpp)

    include(DefaultTargetOptions)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../server)

    target_link_libraries(${TARGET} PRIVATE common common-sdl whisper ${CMAKE_THREAD_LIBS_INIT})

    if (WHISPER_SDL2)
        target_link_libraries(${TARGET} PRIVATE ${SDL2_LIBRARIES})
        target_include_directories(${TARGET} PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()

    if (APPLE)
        target_link_libraries(${TARGET} PRIVATE "-framework Foundation")
        target_link_libraries(${TARGET} PRIVATE "-framework CoreAudio")
        target_link_libraries(${TARGET} PRIVATE "-framework AudioUnit")
        target_link_libraries(${TARGET} PRIVATE "-framework AudioToolbox")
    endif()

    install(TARGETS ${TARGET} RUNTIME)
endif()
