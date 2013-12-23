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
#include <memory>

#include "communicator.h"
#include "background_processor.h"

using namespace std;
using namespace boost::asio;

/// TODO: add result parameters.
typedef boost::function<void(void)> OnBuildCompletedFn;
/// TODO: the targets and options should be parameters.
typedef boost::function<void(const OnBuildCompletedFn&)> OnBuildCmdFn;

/// Communicate with client on a unix socket.
struct Comms {

  Comms(const string& socketName);
  ~Comms();

  void SetOnBuildCmdFn(const OnBuildCmdFn& on_build_cmd);

  private:
  /// Communications take place in their own thread.
  BackgroundProcessor bg_processor_;

  local::stream_protocol::endpoint endpoint_;
  local::stream_protocol::acceptor acceptor_;
  local::stream_protocol::socket socket_;
  std::unique_ptr<Communicator> communicator_;
  boost::array<char, 1024> data_;

  /// Action to be taken when the client requires a build.
  OnBuildCmdFn on_build_cmd_;

  void AsyncAccept();
  void OnAccept(const boost::system::error_code& err);

  void OnBuildRequest(int request_id, const NinjaMessage::BuildRequest& req);
  void OnStopRequest(int request_id, const NinjaMessage::StopRequest& req);

  void OnBuildCompleted(int request_id);
};

#endif  // NINJA_COMMS_H_
