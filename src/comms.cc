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

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "comms.h"

Comms::Comms(const string& socketName)
  : endpoint_(socketName)
  , acceptor_(processor_.Service())
  , socket_(processor_.Service())
  , communicator_(socket_, processor_, processor_) {
  ::unlink(socketName.c_str());
  acceptor_.open(endpoint_.protocol());
  acceptor_.set_option(local::stream_protocol::acceptor::reuse_address(true));
  acceptor_.bind(endpoint_);
  acceptor_.listen();

  AsyncAccept();

  communicator_.SetRequestHandler<NinjaMessage::StopRequest>(
      boost::bind(&Comms::OnStopRequest, this, _1, _2));

  communicator_.SetRequestHandler<NinjaMessage::BuildRequest>(
      boost::bind(&Comms::OnBuildRequest, this, _1, _2));
}

Comms::~Comms() {
}

void Comms::SetOnBuildCmdFn(const OnBuildCmdFn& on_build_cmd)
{
  on_build_cmd_ = on_build_cmd;
}

void Comms::AsyncAccept() {
  acceptor_.async_accept(socket_,
    boost::bind(&Comms::OnAccept, this, boost::asio::placeholders::error));
}

void Comms::OnAccept(const boost::system::error_code& err) {
  printf("Client connected\n");

  if (err) {
    // TODO: handle this error.
    printf("Error: %s\n", err.message().c_str());
    return;
  }

  AsyncRead();
}

void Comms::AsyncRead() {
  socket_.async_read_some(boost::asio::buffer(data_),
    boost::bind(&Comms::OnRead, this, boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
}

void Comms::OnRead(const boost::system::error_code& err,
  size_t bytes_transferred) {

  if (err) {
    printf("Client disconnected\n");
    socket_.close();

    // Wait for client again.
    AsyncAccept();
    return;
  }

  printf("%lu bytes received\n", bytes_transferred);

#if 0
  // Since we do not have protobuf messages for now, we just trigger a build
  // immediately when we receive bytes from the client.
  if (on_build_cmd_) {
    on_build_cmd_(
      processor_.BindPost(boost::bind(&Comms::OnBuildCompleted, this)));
  }
#endif

  // Read again
  AsyncRead();
}

/// This runs on the main thread.
void Comms::OnBuildCompleted(int request_id) {
  printf("Build completed\n");
  NinjaMessage::BuildResponse response;
  communicator_.SendReply(request_id, response);
}

void Comms::OnBuildRequest(int request_id, const NinjaMessage::BuildRequest& req)
{
  printf("OnBuildRequest\n");

  if (on_build_cmd_) {
    on_build_cmd_(
      processor_.BindPost(boost::bind(&Comms::OnBuildCompleted, this, request_id)));
  }
}

void Comms::OnStopRequest(int request_id, const NinjaMessage::StopRequest& req)
{
  printf("OnStopRequest\n");
}
