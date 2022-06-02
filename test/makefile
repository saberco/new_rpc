CXX ?= g++
TARGET1 = server
TARGET2 = client

.PHONY: all
all: $(TARGET1) $(TARGET2)
$(TARGET1): Test.pb.cc servertest.cpp ../common/ProtocolBody.pb.cc ../common/RPCController.cpp ../RpcServer/BusinessWorker.cpp ../RpcServer/IOWorker.cpp ../RpcServer/RpcServer.cpp
	$(CXX) -o $(TARGET1) $^ -pthread -lprotobuf -levent -lboost_thread

$(TARGET2): Test.pb.cc clienttest.cpp ../common/ProtocolBody.pb.cc ../common/RPCController.cpp ../RpcClient/IOWorker.cpp ../RpcClient/RpcChannel.cpp ../RpcClient/RpcClient.cpp 
	$(CXX) -o $(TARGET2) $^ -pthread -lprotobuf -levent -lboost_thread

clean:
	rm -r $(TARGET1)
	rm -r $(TARGET2)

