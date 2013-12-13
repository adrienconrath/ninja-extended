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

#ifndef NINJA_BACKGROUND_PROCESSOR_H_
#define NINJA_BACKGROUND_PROCESSOR_H_

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/thread.hpp>

#include "iprocessor.h"

using namespace boost::asio;

struct BackgroundProcessor : public IProcessor {
  BackgroundProcessor();
  virtual ~BackgroundProcessor();

  virtual void Stop();
  virtual void Post(const ActionFn& fn);
  virtual void Dispatch(const ActionFn& fn);
  virtual io_service& Service();

private:

  virtual void Run();

  boost::asio::io_service io_service_;
  boost::thread thread_;
};

#endif  // NINJA_BACKGROUND_PROCESSOR_H_
