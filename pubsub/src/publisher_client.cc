#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <random>
#include <time.h>
#include "unistd.h"

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
using pubsub::RegisterRequest;
using pubsub::TagMessage;
using pubsub::Pubsub;

class PublisherClient {
 public:
  PublisherClient(std::shared_ptr<Channel> channel)
      : stub_(Pubsub::NewStub(channel)) {}

  long long message_counter = 0;
  int publisher_id = -1;
  int publishing_tag = -1;
  string fixed_tag_text = "";

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

  bool Register() {
    cout << "Input the number of your desired publishing tag\n";
    int tag;
    cin >> tag;
    publishing_tag = tag;

    RegisterRequest request;
    request.set_tag(tag);

    ClientContext context;
    RegisterOK reply;
    Status status = stub_->PublisherRegister(&context, request, &reply);
    publisher_id = reply.publisher_id();

    if (status.ok()) {
      publisher_id = reply.publisher_id();
      fixed_tag_text = reply.fixed_tag_text();
      return true;
    } else {
      cout << "Error\n";
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return false;
    }
  }

  void RunPublisher() {
    ClientContext context;
    PublishOK reply;
    auto writer = std::unique_ptr<ClientWriter<TagMessage>>(stub_->Publish(&context, &reply));

    // exponential distribution is the probability distribution of
    // time between events in a Poisson point process
    //https://en.wikipedia.org/wiki/Exponential_distribution
    exponential_distribution<> d(500/3600.0);
    random_device r;
    mt19937 gen(r());

    TagMessage msg;
    int wait_time;
    for (;;) {
      message_counter++;
      long long msg_id = 10000000 * publisher_id + message_counter;
      time_t current_time = time(NULL);

      msg.set_timestamp(current_time);
      msg.set_message_id(msg_id);
      msg.set_message_tag(publishing_tag);
      msg.set_message_text(fixed_tag_text);

      if (!writer->Write(msg)) break;

        wait_time = d(gen);
        cout << "Sleeping for " << wait_time << "s\n";
        sleep(wait_time);
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

  PublisherClient pubsub(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

  /*std::string user("world");
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;*/

  cout << "Publisher\n";
  cout << "Input the number of your option:\n";
  cout << "1: Get the list of available tags\n2: Skip straight to "
          "registration\n";
  int option;
  cin >> option;

  if (option == 1){
    pubsub.get_tags();
    pubsub.Register();
  }
  else if (option == 2) {
    bool register_ok = false;
    do {
      register_ok = pubsub.Register();
      if (!register_ok) cout << "Error\n";
    } while (!register_ok);

  } else {
    cout << "Invalid option, program exited.\n";
    return 0;
  }

  pubsub.RunPublisher();

  return 0;
}
