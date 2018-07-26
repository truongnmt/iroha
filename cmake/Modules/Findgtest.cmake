add_library(GTest::GTest UNKNOWN IMPORTED)
add_library(GTest::Main UNKNOWN IMPORTED)
add_library(GMock::GMock UNKNOWN IMPORTED)
add_library(GMock::Main UNKNOWN IMPORTED)

find_path(gtest_INCLUDE_DIR gtest/gtest.h)
mark_as_advanced(gtest_INCLUDE_DIR)

find_library(gtest_LIBRARY gtest)
mark_as_advanced(gtest_LIBRARY)

find_library(gtest_MAIN_LIBRARY gtest_main)
mark_as_advanced(gtest_MAIN_LIBRARY)

find_path(gmock_INCLUDE_DIR gmock/gmock.h)
mark_as_advanced(gmock_INCLUDE_DIR)

find_library(gmock_LIBRARY gmock)
mark_as_advanced(gmock_LIBRARY)

find_library(gmock_MAIN_LIBRARY gmock_main)
mark_as_advanced(gmock_MAIN_LIBRARY)

find_package_handle_standard_args(gtest DEFAULT_MSG
    gtest_INCLUDE_DIR
    gtest_LIBRARY
    gtest_MAIN_LIBRARY
    gmock_INCLUDE_DIR
    gmock_LIBRARY
    gmock_MAIN_LIBRARY
    )

set(URL https://github.com/google/googletest)
set(VERSION ec44c6c1675c25b9827aacd08c02433cccde7780)
set_target_description(GTest::GTest "Unit testing library" ${URL} ${VERSION})
set_target_description(GMock::GMock "Mocking library" ${URL} ${VERSION})

if (NOT gtest_FOUND)
  ExternalProject_Add(google_test
      GIT_REPOSITORY ${URL}
      GIT_TAG        ${VERSION}
      CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -Dgtest_force_shared_crt=ON
      -Dgtest_disable_pthreads=OFF
      BUILD_BYPRODUCTS ${EP_PREFIX}/src/google_test-build/googlemock/gtest/libgtest_main.a
                       ${EP_PREFIX}/src/google_test-build/googlemock/gtest/libgtest.a
                       ${EP_PREFIX}/src/google_test-build/googlemock/libgmock_main.a
                       ${EP_PREFIX}/src/google_test-build/googlemock/libgmock.a
      INSTALL_COMMAND "" # remove install step
      UPDATE_COMMAND "" # remove update step
      TEST_COMMAND "" # remove test step
      )
  ExternalProject_Get_Property(google_test source_dir binary_dir)
  set(gtest_INCLUDE_DIR ${source_dir}/googletest/include)
  set(gmock_INCLUDE_DIR ${source_dir}/googlemock/include)

  set(gtest_MAIN_LIBRARY ${binary_dir}/googlemock/gtest/libgtest_main.a)
  set(gtest_LIBRARY ${binary_dir}/googlemock/gtest/libgtest.a)

  set(gmock_MAIN_LIBRARY ${binary_dir}/googlemock/libgmock_main.a)
  set(gmock_LIBRARY ${binary_dir}/googlemock/libgmock.a)

  file(MAKE_DIRECTORY ${gtest_INCLUDE_DIR})
  file(MAKE_DIRECTORY ${gmock_INCLUDE_DIR})

  add_dependencies(GTest::GTest google_test)
  add_dependencies(GTest::Main google_test)
  add_dependencies(GMock::GMock google_test)
  add_dependencies(GMock::Main google_test)
endif ()

set_target_properties(GTest::GTest PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${gtest_INCLUDE_DIR}
    INTERFACE_LINK_LIBRARIES Threads::Threads
    IMPORTED_LOCATION ${gtest_LIBRARY}
    )
set_target_properties(GTest::Main PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${gtest_INCLUDE_DIR}
    INTERFACE_LINK_LIBRARIES GTest::GTest
    IMPORTED_LOCATION ${gtest_MAIN_LIBRARY}
    )

set_target_properties(GMock::GMock PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${gmock_INCLUDE_DIR}
    INTERFACE_LINK_LIBRARIES Threads::Threads
    IMPORTED_LOCATION ${gmock_LIBRARY}
    )
set_target_properties(GMock::Main PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${gmock_INCLUDE_DIR}
    INTERFACE_LINK_LIBRARIES GMock::GMock
    IMPORTED_LOCATION ${gmock_MAIN_LIBRARY}
    )
