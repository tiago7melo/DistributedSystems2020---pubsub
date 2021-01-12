#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <time.h>
#include "unistd.h"

#include "pubsub.grpc.pb.h"

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
  std::map<int,vector<Subscriber>> tag_to_subscriber;  // tag, Streams
  std::mutex mtx[MAX_TAG_SIZE+1]; 
  std::map<int, deque<TagMessage>> message_database;

  long long message_lifespan =  60; // in seconds;
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
    //check if tag is valid
    if (tag >= 1 && tag <= MAX_TAG_SIZE) {
      subscriber_counter++;
      sub.id = subscriber_counter;
      sub.writer = stream;

      unique_lock<mutex> sub_lock(mtx[tag]);
      tag_to_subscriber[tag].push_back(sub);
      sub_lock.unlock();

      //fetching messages of tag in database
      ClearExpiredMessages(tag);
      cout << "Cleared old messages\n";
      for(TagMessage msg : message_database[tag])
          sub.writer->Write(msg); 


      // loop that keeps the ServerWriter open until cancellation
      for (;;) {
        if(context->IsCancelled()) {
          cout << "A subscriber for tag " << tag << " has disconnected" << "\n";
          break;
        }
        sleep(1);
      }
      cancel_subscriber(tag,sub.id);
    }
    else { 
      cout << "   Failed, tag out of bounds\n";
      return Status::CANCELLED; 
    }
    return Status::OK;
  }

  void cancel_subscriber(int tag, int id) {
      //client cancelled, remove from the tag_to_subscriber map
      unique_lock<mutex> cancel_lock(mtx[tag]);
      auto delete_pos = tag_to_subscriber[tag].end();
      for(auto entry = tag_to_subscriber[tag].begin(); entry != tag_to_subscriber[tag].end(); entry++) {
        if (entry->id == id) {
          cout << "   Removing subscriber " << entry->id << " from registry\n";
          delete_pos = entry;
          break;
        }
      }
      tag_to_subscriber[tag].erase(delete_pos);
      cancel_lock.unlock();
      return;
  }

  Status PublisherRegister(ServerContext* context, const RegisterRequest* request,
                           RegisterOK* reply) override {
    int tag = request->tag();
    cout << "Registering new publisher for tag " << tag << "\n";
    if (tag >= 1 && tag <= MAX_TAG_SIZE) {
      publisher_counter++;
      reply->set_publisher_id(publisher_counter);
      reply->set_fixed_tag_text(tag_text[tag - 1]);
    }
    else return Status::CANCELLED;

    return Status::OK;
  }

  Status Publish(ServerContext* context, ServerReader<TagMessage>* publisher_stream,
                 PublishOK* reply) override {
    // loop where broker gets fed messages
    // and redirects to the subscribers of tag, until cancellation
    int target_tag;
    TagMessage msg;
    while (publisher_stream->Read(&msg) && context->IsCancelled() == false) {
      target_tag = msg.message_tag();
      std::unique_lock<mutex> write_lock(mtx[target_tag]);

      for (Subscriber subscriber : tag_to_subscriber[target_tag]) {
        ServerWriter<TagMessage>* subscriber_stream = subscriber.writer;
        subscriber_stream->Write(msg);
      }
      cout << "Storing message for tag " << target_tag << "\n";
      message_database[target_tag].push_back(msg);
      write_lock.unlock();
    }

    cout << "A publisher for tag " << target_tag << " has disconnected\n";
    return Status::OK;
  }

  bool isExpiredMessage(TagMessage msg) {
      time_t now = time(NULL);
      time_t msg_time = msg.timestamp();
      time_t time_elapsed = now - msg_time;
      return time_elapsed > message_lifespan;
  }

  void ClearExpiredMessages(int tag) {
      unique_lock<mutex> clear_lock(mtx[tag]);
      while(!message_database[tag].empty()) {
          auto entry = message_database[tag].begin();
          TagMessage msg = *entry;
          
          if(!isExpiredMessage(msg)) break;

          message_database[tag].pop_front();

          time_t timestamp = msg.timestamp();
          string timestamp_str = ctime(&timestamp);
          cout << "Deleted message with ID " <<  msg.message_id() 
          << " and timestamp " << timestamp_str << "\n";
      }
      clear_lock.unlock();
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
