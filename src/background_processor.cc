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

#include "background_processor.h"

BackgroundProcessor::BackgroundProcessor()
  : thread_(boost::bind(&BackgroundProcessor::Run, this))
  , explicit_stop_(false) {

}

BackgroundProcessor::~BackgroundProcessor() {
  thread_.join();
}

io_service& BackgroundProcessor::Service()
{
  return io_service_;
}

void BackgroundProcessor::Run() {
  while (true) {
    boost::system::error_code err;
    io_service_.run(err);
    if (err) {
      // TODO: handle this error.
      printf("Error: %s\n", err.message().c_str());
    }
    if (explicit_stop_)
      break;
    io_service_.reset();
  }
}

void BackgroundProcessor::Stop() {
  explicit_stop_ = true;
  io_service_.stop();
}

void BackgroundProcessor::Post(const ActionFn& fn) {
  io_service_.post(fn);
}

void BackgroundProcessor::Dispatch(const ActionFn& fn) {
  io_service_.dispatch(fn);
}
