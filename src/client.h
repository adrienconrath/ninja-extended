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
#include <memory>

#include "communicator.h"
#include "processor.h"
#include "background_processor.h"

using namespace std;
using namespace boost::asio;

struct Client {
  private:

    typedef boost::function<void(bool)> OnConnectCompletedFn;
    typedef boost::function<void(const RequestResult&,
        const NinjaMessage::BuildResponse&)> OnBuildCompletedFn;

  public:

    Client(string socket_name);
    ~Client();

    void Run();

    void AsyncConnect(const OnConnectCompletedFn& onConnectCompleted);
    bool Connect();

    void AsyncBuild(const OnBuildCompletedFn& onBuildCompleted);
    void Build();

  private:
    bool connected_;
    bool continue_;
    // TODO: this should be a background processor.
    Processor processor_;
    BackgroundProcessor bg_processor_;
    string socket_name_;
    local::stream_protocol::endpoint endpoint_;
    local::stream_protocol::socket socket_;
    std::unique_ptr<Communicator> communicator_;

    void OnConnectCompleted(const OnConnectCompletedFn& onConnectCompleted,
        boost::system::error_code err);
};

#endif // NINJA_CLIENT_H_

