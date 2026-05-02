set(gtest_force_shared_crt ON)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(qextra_test store.cpp)
target_link_libraries(qextra_test PRIVATE qextra GTest::gtest_main)
target_compile_features(qextra_test PRIVATE cxx_std_23)
set_target_properties(qextra_test PROPERTIES AUTOMOC ON)

include(GoogleTest)
gtest_discover_tests(qextra_test)
