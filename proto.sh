protoc --grpc_out=api/ --cpp_out=api/ --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
    shorturl.proto -I .