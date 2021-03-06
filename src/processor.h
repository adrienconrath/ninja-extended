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

#ifndef NINJA_PROCESSOR_H_
#define NINJA_PROCESSOR_H_

#include <boost/asio.hpp>
#include <boost/function.hpp>

#include "iprocessor.h"

using namespace boost::asio;

struct Processor : public IProcessor {
  Processor();
  virtual ~Processor();

  virtual void Stop();
  virtual void Run();
  virtual void RunOne();
  virtual void Post(const ActionFn& fn);
  virtual void Dispatch(const ActionFn& fn);
  virtual io_service& Service();
  virtual const ActionFn BindPost(const ActionFn& fn);

private:
  boost::asio::io_service io_service_;
  boost::asio::io_service::work work_;
};

#endif  // NINJA_PROCESSOR_H_
