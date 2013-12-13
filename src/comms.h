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

#ifndef NINJA_COMMS_H_
#define NINJA_COMMS_H_

#include <string>
#include <boost/asio.hpp>
#include <boost/array.hpp>

#include "background_processor.h"

using namespace std;
using namespace boost::asio;

/// Communicate with client on a unix socket.
struct Comms {

  Comms(const string& socketName);
  ~Comms();

private:
  /// Communications take place in their own thread.
  BackgroundProcessor processor_;

  local::stream_protocol::endpoint endpoint_;
  local::stream_protocol::acceptor acceptor_;
  local::stream_protocol::socket socket_;
  boost::array<char, 1024> data_;

  void AsyncAccept();
  void OnAccept(const boost::system::error_code& err);
  void AsyncRead();
  void OnRead(const boost::system::error_code& err, size_t bytes_transferred);
};

#endif  // NINJA_COMMS_H_