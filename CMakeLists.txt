project(repro)
cmake_minimum_required(VERSION 3.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

SET(BOOST_ROOT /home/sehe/custom/boost_1_78_0)
find_package(Boost 1.65.0 COMPONENTS system)

ADD_DEFINITIONS(-DBOOST_ALLOW_DEPRECATED_HEADERS=1)

SET(CMAKE_C_COMPILER gcc-10)
SET(CMAKE_CXX_COMPILER g++-10)

LINK_LIBRARIES(boost_system)
LINK_LIBRARIES(boost_random)
LINK_LIBRARIES(boost_json)
LINK_LIBRARIES(boost_date_time)
LINK_LIBRARIES(boost_chrono)
LINK_LIBRARIES(boost_context)
LINK_LIBRARIES(boost_container)
#LINK_LIBRARIES(boost_fiber)
LINK_LIBRARIES(boost_coroutine)
LINK_LIBRARIES(boost_locale)
##LINK_LIBRARIES(boost_random)
LINK_LIBRARIES(boost_filesystem)
LINK_LIBRARIES(boost_iostreams)
LINK_LIBRARIES(boost_program_options)
LINK_LIBRARIES(boost_regex)
LINK_LIBRARIES(boost_serialization)
#LINK_LIBRARIES(boost_stacktrace_backtrace)
LINK_LIBRARIES(boost_log_setup)
LINK_LIBRARIES(boost_log)
LINK_LIBRARIES(boost_thread)
LINK_LIBRARIES(gmp)
LINK_LIBRARIES(png)
LINK_LIBRARIES(rt)
LINK_LIBRARIES(ssl)
LINK_LIBRARIES(crypto)

LINK_LIBRARIES(dl)

INCLUDE_DIRECTORIES(SYSTEM ${Boost_INCLUDE_DIRS})
LINK_DIRECTORIES("${Boost_LIBRARY_DIRS}")

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic ") # -Wconversion
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g0 -O3 -pthread -march=native")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -fno-omit-frame-pointer -O0 -pthread -march=x86-64")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")

ADD_EXECUTABLE(sotest test.cpp
    server.cpp
    connection.cpp
    connection_manager.cpp
)

ADD_CUSTOM_COMMAND(
    OUTPUT .notified
    DEPENDS sotest 
    #COMMAND notify-send -u low -t 2000 CMake "Build complete"
    COMMAND play -q /usr/share/sounds/purple/alert.wav
)
#ADD_CUSTOM_TARGET(notify ALL DEPENDS .notified sotest)
SET(TIDYCHECKS "-*")
SET(TIDYCHECKS "${TIDYCHECKS},readability-*")
SET(TIDYCHECKS "${TIDYCHECKS},modernize-*,-modernize-use-trailing-return-type")
SET(TIDYCHECKS "${TIDYCHECKS},hicpp-*")
SET(TIDYCHECKS "${TIDYCHECKS},bugprone-*")
SET(TIDYCHECKS "${TIDYCHECKS},cppcoreguidelines-*")
SET(TIDYCHECKS "${TIDYCHECKS},google-*")
SET(TIDYCHECKS "${TIDYCHECKS},performance-*,")

ADD_CUSTOM_COMMAND(OUTPUT .tidy-all
    COMMAND run-clang-tidy-11 -fix -checks='${TIDYCHECKS}' test.cpp)
ADD_CUSTOM_TARGET(tidy-all DEPENDS .tidy-all)

ADD_CUSTOM_COMMAND(OUTPUT .stamp
    COMMAND run-clang-tidy-11
    -fix
    -checks='-*,readability-*,modernize-*,-modernize-use-trailing-return-type'
    test.cpp
)
    ##when newer clang-tidy:
    #--format-style=file
ADD_CUSTOM_TARGET(tidy DEPENDS .stamp)

