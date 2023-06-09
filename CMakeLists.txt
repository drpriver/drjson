cmake_minimum_required(VERSION 3.14) # This is a guess
project(DrJson VERSION 3.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

if(WIN32)
add_compile_options("-D_CRT_NONSTDC_NO_DEPRECATE" "-D_CRT_SECURE_NO_WARNINGS")
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
add_compile_options("/wd4244" "/wd5105" "/wd4146" "/wd4267" "/wd4018" "/wd4141" "/wd4163")
endif()
elseif(APPLE)
elseif(UNIX)
set(LIBM_LIBRARIES m)
endif()

add_library(drjson-dylib
    SHARED
	DrJson/drjson.c
)
set_target_properties(drjson-dylib
    PROPERTIES OUTPUT_NAME drjson
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
	PUBLIC_HEADER "DrJson/drjson.h"
)
add_library(drjson-lib
    STATIC
	DrJson/drjson.c
)
if(WIN32)
set_target_properties(drjson-lib
    PROPERTIES
	OUTPUT_NAME drjson-static
)
else()
set_target_properties(drjson-lib
    PROPERTIES
	OUTPUT_NAME drjson
)
endif()
set_target_properties(drjson-lib
	PROPERTIES
    VERSION ${PROJECT_VERSION}
)

add_executable(drjson DrJson/drjson_cli.c)
target_link_libraries(drjson-lib ${LIBM_LIBRARIES})
target_link_libraries(drjson-dylib ${LIBM_LIBRARIES})

install(TARGETS drjson-lib LIBRARY DESTINATION lib)
install(TARGETS drjson-dylib
		LIBRARY DESTINATION lib
		PUBLIC_HEADER DESTINATION include/DrJson)
install(TARGETS drjson DESTINATION bin)

add_executable(test-drjson DrJson/test_drjson.c)
target_link_libraries(test-drjson drjson-dylib)

enable_testing()
add_test(test-drjson test-drjson)

