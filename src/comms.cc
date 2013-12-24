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
  : stopped_(false), endpoint_(socketName)
    , acceptor_(bg_processor_.Service())
    , socket_(bg_processor_.Service()) {
      ::unlink(socketName.c_str());
      acceptor_.open(endpoint_.protocol());
      acceptor_.set_option(local::stream_protocol::acceptor::reuse_address(true));
      acceptor_.bind(endpoint_);
      acceptor_.listen();

      AsyncAccept();
    }

Comms::~Comms() {
  // TODO: verify that the communicator_ has been stopped before destroying
  // this object.
}

void Comms::SetOnStopCmdFn(const OnStopCmdFn& on_stop_cmd)
{
  on_stop_cmd_ = on_stop_cmd;
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
  if (err) {
    if (err == boost::asio::error::operation_aborted) {
      // The daemon was stopped.
    }
    else {
      // TODO: handle this error.
      printf("Error: %s\n", err.message().c_str());
    }
    return;
  }

  // Create the communicator
  communicator_.reset(new Communicator(socket_, bg_processor_, bg_processor_));
  communicator_->SetOnConnectionClosedFn(
      boost::bind(&Comms::OnConnectionClosed, this));

  // Set the request handlers
  communicator_->SetRequestHandler<NinjaMessage::StopRequest>(
      boost::bind(&Comms::OnStopRequest, this, _1, _2));
  communicator_->SetRequestHandler<NinjaMessage::BuildRequest>(
      boost::bind(&Comms::OnBuildRequest, this, _1, _2));
}

void Comms::OnConnectionClosed() {
  socket_.shutdown(local::stream_protocol::socket::shutdown_both);
  socket_.close();
  communicator_.reset(0);

  if (!stopped_) {
    // When the connection is closed, we can start accepting again.
    AsyncAccept();
  }
}

void Comms::OnBuildCompleted(int request_id) {
  NinjaMessage::BuildResponse response;
  communicator_->SendReply(request_id, response);
}

void Comms::OnBuildRequest(int request_id, const NinjaMessage::BuildRequest& req)
{
  if (on_build_cmd_) {
    on_build_cmd_(
        bg_processor_.BindPost(boost::bind(&Comms::OnBuildCompleted, this, request_id)));
  }
}

void Comms::OnCloseCompleted() {
  if (on_stop_cmd_) {
    on_stop_cmd_();
  }
}

void Comms::OnStopRequest(int request_id, const NinjaMessage::StopRequest& req)
{
  // Response immediately.
  NinjaMessage::StopResponse response;
  communicator_->SendReply(request_id, response);

  stopped_ = true;
  communicator_->AsyncClose(boost::bind(&Comms::OnCloseCompleted, this));
}
