find_package(Qt6 REQUIRED COMPONENTS Quick ProtobufQuick ProtobufWellKnownTypes)

add_library(qt STATIC)
add_library(qextra STATIC src/asio/context.cpp)

target_sources(
  qt
  PUBLIC FILE_SET
         all
         TYPE
         CXX_MODULES
         FILES
         src/qt.cppm)

target_sources(
  qextra
  PUBLIC FILE_SET
         all
         TYPE
         CXX_MODULES
         FILES
         src/mod.cppm
         src/kstore.cppm
         src/async.cppm
         src/helper.cppm
         src/bindable.cppm
         src/asio/mod.cppm
         src/asio/task.cppm
         src/asio/forward.cppm
         src/asio/context.cppm
         src/asio/executor.cppm
         src/asio/watcher.cppm
         src/asio/watch_dog.cppm
         )

set_target_properties(qextra PROPERTIES AUTOMOC ON)
split_moc_files(
  TARGET
  qextra
  BASE_DIR
  src
  PREFIX
  QExtra
  MOC_SOURCE_FILES
  src/async.cppm)

target_sources(qextra PRIVATE src/async.cpp 
         src/asio/mod.cpp)
set_source_files_properties(src/async.cpp PROPERTIES SKIP_AUTOMOC ON)

target_compile_features(qt PUBLIC cxx_std_20)
target_link_libraries(qt PUBLIC Qt6::Core Qt6::Quick)

target_compile_features(qextra PUBLIC cxx_std_20)
target_link_libraries(qextra PUBLIC qt ncrequest kstore::kstore)
target_include_directories(qextra PUBLIC include)
