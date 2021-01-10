#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

#ifdef BAZEL_BUILD
#include "examples/protos/pubsub.grpc.pb.h"
#else
#include "pubsub.grpc.pb.h"
#endif

using namespace std;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;
using pubsub::ListReply;
using pubsub::ListRequest;
using pubsub::PublishOK;
using pubsub::Pubsub;
using pubsub::RegisterOK;
using pubsub::RegisterRequest;
using pubsub::SubscribeRequest;
using pubsub::TagMessage;

#define MAX_TAG_SIZE 4

struct Subscriber {
  long long id;
  ServerWriter<TagMessage>* writer;
};

class PubsubServiceImpl final : public Pubsub::Service {
  std::map<int, vector<Subscriber>> tag_to_client;  // tag, Streams
  std::mutex mtx[MAX_TAG_SIZE]; 
  long long publisher_counter = 0;
  long long subscriber_counter = 0;
  std::string tag_text[MAX_TAG_SIZE] = {
      "Trial version downloaded", "License purchased",
      "Support service requested", "Bug reported"};

  Status ShowTagList(ServerContext* context, const ListRequest* request,
                     ListReply* reply) override {
    reply->set_tag_list("1: Trial\n2: License\n3: Support\n4: Bug\n");
    return Status::OK;
  }

  Status TagSubscribe(ServerContext* context, const SubscribeRequest* request,
                      ServerWriter<TagMessage>* stream) override {
    int tag = request->tag();
    cout << "Registering new subscriber for tag " << tag << "\n";
    Subscriber sub;
    //check if tag if valid
    if (tag >= 1 && tag <= MAX_TAG_SIZE) {
      subscriber_counter++;
      sub.id = subscriber_counter;
      sub.writer = stream;

      unique_lock<mutex> write_lock(mtx[tag]);
      tag_to_client[tag].push_back(sub);
      write_lock.unlock();

      // loop that keeps the ServerWriter open until cancellation
      for (;;) {
        if(context->IsCancelled()) {
          cout << "A subscriber for tag " << tag <<" has disconnected" << "\n";
          break;
        }
      }
      
      //client cancelled, remove from the tag_to_client map
      unique_lock<mutex> del_lock(mtx[tag]);
      auto delete_pos = tag_to_client[tag].end();
      for(auto s = tag_to_client[tag].begin(); s != tag_to_client[tag].end(); s++) {
        if (s->id == sub.id) {
          cout << "   Removing subscriber " << s->id << " from registry\n";
          delete_pos = s;
          break;
        }
      }
      tag_to_client[tag].erase(delete_pos);
      del_lock.unlock();
    } 
    else return Status::CANCELLED; //return error if tag is not valid
    return Status::OK;
  }

  Status PublisherRegister(ServerContext* context, const RegisterRequest* request,
                           RegisterOK* reply) override {
    int tag = request->tag();
    cout << "Registering new publisher for tag " << tag << "\n";
    if (tag >= 1 && tag <= MAX_TAG_SIZE) {
      publisher_counter++;
      reply->set_publisher_id(publisher_counter);
      reply->set_fixed_tag_text(tag_text[tag - 1]);
    } else
      reply->set_publisher_id(-42);

    return Status::OK;
  }

  Status Publish(ServerContext* context, ServerReader<TagMessage>* publisher_stream,
                 PublishOK* reply) override {
    TagMessage msg;
    // loop where it gets fed messages
    // and redirects to the subscribers of tag, until cancellation
    int target_tag;
    while (publisher_stream->Read(&msg) && context->IsCancelled() == false) {
      target_tag = msg.message_tag();
      std::unique_lock<mutex> write_lock(mtx[target_tag]);
      for (Subscriber subscriber : tag_to_client[target_tag]) {
        ServerWriter<TagMessage>* subscriber_stream = subscriber.writer;
        subscriber_stream->Write(msg);
      }
      write_lock.unlock();
    }

    cout << "A publisher for tag " << target_tag << " has disconnected\n";
    return Status::OK;
  }
};

void RunBroker() {
  std::string server_address("0.0.0.0:57575");
  PubsubServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  RunBroker();

  return 0;
}
