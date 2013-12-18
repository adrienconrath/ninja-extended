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

#ifndef NINJA_CLIENT_H_
#define NINJA_CLIENT_H_

#include <string>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>

#include "communicator.h"
#include "processor.h"

using namespace std;
using namespace boost::asio;

typedef boost::function<void(bool)> OnConnectCompletedFn;
typedef boost::function<void(bool)> OnCommandCompletedFn;

struct Client {
  Client(string socket_name);

  void Run();

  void AsyncConnect(const OnConnectCompletedFn& onConnectCompleted);
  bool Connect();

  /// TODO: pass the command as a protobuf message
  void AsyncSendCommand(const OnCommandCompletedFn& onCommandCompleted);
  bool SendCommand();

  private:
  bool connected_;
  bool continue_;
  // TODO: this should be a background processor.
  Processor processor_;
  string socket_name_;
  local::stream_protocol::endpoint endpoint_;
  local::stream_protocol::socket socket_;
  Communicator communicator_;

  boost::array<char, 1> dummy_data_;

  void OnConnectCompleted(const OnConnectCompletedFn& onConnectCompleted,
      boost::system::error_code err);
  void OnCommandCompleted(const OnCommandCompletedFn& OnCommandCompleted,
      boost::system::error_code err, size_t bytes_transferred);

  void SendBuildRequest();
  void OnBuildCompleted(const RequestResult& res,
      const NinjaMessage::BuildResponse& response);
};

#endif // NINJA_CLIENT_H_

