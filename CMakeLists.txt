# Copyright 2026 L. Richard Moore Jr.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.18)
project(oms VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# In a standalone build, make this directory's headers findable by plain name.
# When built via add_subdirectory(oms) the parent's include_directories(..)
# already resolves both "Oms.hpp" and "commons/Ensure.hpp" through the symlinks.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ensure/Ensure.hpp")
        include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ensure)
    endif()
endif()

find_package(Catch2 3 REQUIRED)

add_executable(Oms-test Oms-test.cpp Oms.hpp OmsString.hpp)
target_link_libraries(Oms-test PRIVATE Catch2::Catch2WithMain)

add_executable(odump OmsDump.cpp Oms.hpp OmsString.hpp)

enable_testing()
include(Catch)
catch_discover_tests(Oms-test)
