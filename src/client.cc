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
  socket_(processor_.Service()),
  communicator_(socket_, bg_processor_, processor_) {
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
    printf("Error: %s\n", err.message().c_str());
    onConnectCompleted(false);
    return;
  } 

  connected_ = true;
  onConnectCompleted(true);
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

void Client::OnCommandCompleted(const OnCommandCompletedFn& onCommandCompleted,
    boost::system::error_code err, size_t bytes_transferred) {
  if (err)
  {
    printf("Error: %s\n", err.message().c_str());
    onCommandCompleted(false);
    return;
  }

  onCommandCompleted(true);
}

void Client::SendBuildRequest() {
  NinjaMessage::BuildRequest req;
  communicator_.SendRequest<NinjaMessage::BuildRequest, NinjaMessage::BuildResponse>(
      req, boost::bind(&Client::OnBuildCompleted, this, _1, _2));
}

void Client::OnBuildCompleted(const RequestResult& res,
    const NinjaMessage::BuildResponse& response) {
  printf("Client::OnBuildCompleted\n");
}

void Client::AsyncSendCommand(const OnCommandCompletedFn& onCommandCompleted) {
  assert(connected_);
  async_write(socket_, buffer(dummy_data_), boost::bind(&Client::OnCommandCompleted,
        this, onCommandCompleted, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}

bool Client::SendCommand() {
  bool success = false;
  bool completed = false;

  OnCommandCompletedFn fn = [&success, &completed](bool res) {
    success = res;
    completed = true;
  };

  AsyncSendCommand(fn);
  while (!completed) {
    processor_.RunOne();
  }

  return success;
}
