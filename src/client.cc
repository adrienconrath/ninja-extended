// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "client.h"

Client::Client(string socket_name)
  : connected_(false), continue_(false),
  socket_name_(socket_name), endpoint_(socket_name_),
  socket_(bg_processor_.Service()) {
  }

Client::~Client() {
  // Close the socket.
  socket_.shutdown(local::stream_protocol::socket::shutdown_both);
  socket_.close();
}

void Client::Run() {
  while (continue_) {
    processor_.RunOne();
  }
}

void Client::OnConnectCompleted(const OnConnectCompletedFn& onConnectCompleted,
    boost::system::error_code err) {
  if (err)
  {
    // TODO: pass error code.
    processor_.Post(boost::bind(onConnectCompleted, false));
    return;
  }

  // Create the communicator
  communicator_.reset(new Communicator(socket_, bg_processor_, processor_));

  connected_ = true;
  processor_.Post(boost::bind(onConnectCompleted, true));
}

void Client::AsyncConnect(const OnConnectCompletedFn& onConnectCompleted) {
  assert(!connected_);

  socket_.async_connect(endpoint_,
      boost::bind(&Client::OnConnectCompleted, this, onConnectCompleted,
        boost::asio::placeholders::error));
}

bool Client::Connect() {
  bool success = false;
  bool completed = false;

  OnConnectCompletedFn fn = [&success, &completed](bool res) {
    success = res;
    completed = true;
  };

  AsyncConnect(fn);
  while (!completed) {
    processor_.RunOne();
  }

  return success;
}

void Client::AsyncBuild(const OnBuildCompletedFn& onBuildCompleted) {
  NinjaMessage::BuildRequest req;
  communicator_->SendRequest<NinjaMessage::BuildRequest, NinjaMessage::BuildResponse>(
      req, onBuildCompleted);
}

void Client::Build() {
  bool completed = false;

  OnBuildCompletedFn fn = [&completed](const RequestResult& res,
    const NinjaMessage::BuildResponse& response) {
    printf("Client: build completed\n");
    completed = true;
    // Ignore the response for now.
  };

  AsyncBuild(fn);
  while (!completed) {
    processor_.RunOne();
  }
}

