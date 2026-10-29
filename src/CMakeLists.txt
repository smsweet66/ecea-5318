add_subdirectory(util)

add_executable(clock-reader)
target_sources(clock-reader
PRIVATE
    main.cpp
)
target_link_libraries(clock-reader util)
