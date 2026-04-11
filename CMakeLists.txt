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
         src/helper.cppm
         src/qobject_bindable_property_p.cppm
         src/asio/mod.cppm
         src/asio/forward.cppm
         src/asio/context.cppm
         src/asio/executor.cppm
         src/asio/watcher.cppm)

target_compile_features(qt PUBLIC cxx_std_20)
target_link_libraries(qt PUBLIC Qt6::Core Qt6::Quick)

target_compile_features(qextra PUBLIC cxx_std_20)
target_link_libraries(qextra PUBLIC qt ncrequest kstore::kstore)
