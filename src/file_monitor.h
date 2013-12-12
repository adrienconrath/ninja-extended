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
#include <sys/select.h>
#include <sys/inotify.h>

using namespace std;

struct Node;
struct State;

/// Monitors files for changes.
/// XXX: the event queue can overflow: how to handle that?
/// TODO: create an interface to ease unit testing.
struct FileMonitor {
  /// Create a monitor associated with the given state.
  FileMonitor(State* state);

  FileMonitor(const FileMonitor&);
  FileMonitor& operator=(const FileMonitor&);

  /// Update the monitor.
  bool Load(string* err);

  bool Wait();

  /// A node is not dirty anymore and needs to be monitored.
  bool MonitorNode(Node* node, string* err);

private:
  State* state_;
  unordered_map<int, Node*> map_fds_;
  unordered_map<Node*, int> map_nodes_;
  int inotify_fd_;
  fd_set inotify_fds_;

  bool LoadSubTarget(Node* node, string* err);
  bool AddNode(Node* node, string* err);
  bool HandleEvent(struct inotify_event* event);
  bool MarkNodeDirty(Node* node, int wd, string* err);
  bool MarkOutputDirty(Node* node, string* err);
};

#endif // NINJA_FILE_MONITOR_H_
