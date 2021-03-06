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

#include <boost/bind.hpp>

#include "processor.h"

Processor::Processor() : work_(io_service_) {

}

Processor::~Processor() {

}

io_service& Processor::Service()
{
  return io_service_;
}

void Processor::Stop() {
  io_service_.stop();
}

/// Will block.
void Processor::Run() {
  io_service_.run();
}

/// Will block.
void Processor::RunOne() {
  boost::system::error_code err;
  io_service_.run_one(err);
  if (err) {
    // TODO: handle this error.
    printf("Error: %s\n", err.message().c_str());
  }
  else {
    io_service_.reset();
  }
}

void Processor::Post(const ActionFn& fn) {
  io_service_.post(fn);
}

void Processor::Dispatch(const ActionFn& fn) {
  io_service_.dispatch(fn);
}

const ActionFn Processor::BindPost(const ActionFn& fn) {
  return boost::bind(&Processor::Post, this, fn);
}
