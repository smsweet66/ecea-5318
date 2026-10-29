add_library(util)
target_include_directories(util PUBLIC include)
target_sources(util
PRIVATE
    util.cpp
)
