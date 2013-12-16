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

#ifndef NINJA_FILE_MONITOR_H_
#define NINJA_FILE_MONITOR_H_

#include <string>
#include <unordered_map>
#include <vector>
#include <sys/select.h>
#include <sys/inotify.h>

#include <boost/shared_ptr.hpp>

#include "background_processor.h"

#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

using namespace std;

struct Node;
struct State;

/// TODO: typedef boost::function<void(void)> OnQueueOverflowFn;

/// Monitors files for changes.
/// XXX: the event queue can overflow: how to handle that?
/// TODO: create an interface to ease unit testing.
struct FileMonitor {
  typedef boost::function<void(boost::shared_ptr<vector<Node*>>)>
    OnFlushCompletedFn;

  /// Create a monitor associated with the given state.
  FileMonitor(State* state);

  /// Update the monitor.
  bool Start(string* err);

  /// A node is not dirty anymore and needs to be monitored.
  void MonitorNode(Node* node);
  /// Flush the queue of changed files.
  void Flush(const OnFlushCompletedFn& onCompleted);

private:
  /// File monitoring takes place in its own thread.
  BackgroundProcessor processor_;

  State* state_;
  unordered_map<int, Node*> map_fds_;
  unordered_map<Node*, int> map_nodes_;
  int inotify_fd_;
  fd_set inotify_fds_;
  char buffer_[EVENT_BUF_LEN];
  boost::asio::posix::stream_descriptor stream_;

  /// Keep track of files that have changed.
  boost::shared_ptr<vector<Node*>> changed_files_;

  void AsyncRead();
  void HandleRead(boost::system::error_code err, std::size_t bytes_transferred);

  bool LoadSubTarget(Node* node, string* err);
  bool AddNode(Node* node, string* err);
  bool HandleEvent(struct inotify_event* event);
  bool MarkNodeDirty(Node* node, int wd, string* err);
  void MonitorNodeBgThread(Node* node);
  void FlushBgThread(const OnFlushCompletedFn& onCompleted);
};

#endif // NINJA_FILE_MONITOR_H_
