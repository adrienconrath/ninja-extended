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
  , socket_(processor_.Service()) {
  ::unlink(socketName.c_str());
  acceptor_.open(endpoint_.protocol());
  acceptor_.set_option(local::stream_protocol::acceptor::reuse_address(true));
  acceptor_.bind(endpoint_);
  acceptor_.listen();

  AsyncAccept();
}

Comms::~Comms() {
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

  // Read again
  AsyncRead();
}