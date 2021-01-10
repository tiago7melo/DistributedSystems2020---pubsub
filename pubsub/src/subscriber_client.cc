#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <time.h>

#ifdef BAZEL_BUILD
#include "examples/protos/pubsub.grpc.pb.h"
#else
#include "pubsub.grpc.pb.h"
#endif

using namespace std;

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using pubsub::ListReply;
using pubsub::ListRequest;
using pubsub::PublishOK;
using pubsub::RegisterOK;
using pubsub::SubscribeRequest;
using pubsub::TagMessage;
using pubsub::Pubsub;

class SubscriberClient {
 public: SubscriberClient(std::shared_ptr<Channel> channel)
      : stub_(Pubsub::NewStub(channel)) {}

  void get_tags() {
    ClientContext context;
    ListRequest request;
    ListReply reply;

    request.set_ok(false);

    Status status = stub_->ShowTagList(&context, request, &reply);

    if (status.ok())
      cout << reply.tag_list();
    else
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
  }

  bool subscribe() {
    cout << "Input the number of your desired subscription tag\n";
    int tag;
    cin >> tag;

    SubscribeRequest request;
    ClientContext context;

    request.set_tag(tag);
    auto reader = std::unique_ptr<ClientReader<TagMessage>>(stub_->TagSubscribe(&context, request));

    TagMessage msg;
    while (reader->Read(&msg)) {
      string text = msg.message_text();
      int id = msg.message_id();
      int tag = msg.message_tag();
      time_t timestamp = msg.timestamp();
      string timestamp_string = ctime(&timestamp);

      cout << "TAG: " << tag << " ID: " << id << " | " << timestamp_string << "\n"
           << text << "\n";
      cout << "\n-------------\n";
    }

    Status status = reader->Finish();
    if (status.ok()) {
      std::cout << "Streaming succeeded." << std::endl;
      return true;
    } else {
      std::cout << "Streaming failed." << std::endl;
      return false;
    }
  }

  void RunSubscriber() {
    cout << "Subscriber\n";
    cout << "Input the number of your option:\n";
    cout << "1: Get the list of available tags\n2: Skip straight to "
            "subscription\n";
    int option;
    cin >> option;

    if (option == 1) {
      get_tags();
      subscribe();
    }
    else if (option == 2) {
      bool subscribe_ok = false;
      do {
        subscribe_ok = subscribe();

        if(!subscribe_ok) cout << "Error\n";
      } while (!subscribe_ok);
    } else {
      cout << "Invalid option, program exited.\n";
      return;
    }
  }

 private:
  std::unique_ptr<Pubsub::Stub> stub_;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).

  string target_str = "localhost:57575";
  SubscriberClient pubsub(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

  /*std::string user("world");
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;*/

  pubsub.RunSubscriber();

  return 0;
}
