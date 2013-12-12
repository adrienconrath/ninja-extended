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

#include "file_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unordered_map>

#include "graph.h"
#include "state.h"
#include "util.h"
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

FileMonitor::FileMonitor(State* state)
  : state_(state) {
    inotify_fd_ = inotify_init();
}

bool FileMonitor::Load(string* err) {
  if (inotify_fd_ < 0){
    *err = "Cannot initialize inotify";
    return false;
  }

  vector<Node*> roots = state_->RootNodes(err);

  for (vector<Node*>::iterator i = roots.begin(); i != roots.end(); ++i) {
    if ((*i)->dirty())
      continue; // No need to monitor a dirty node.
    if (!LoadSubTarget(*i, err))
      return false;
  }

  return true;
}

bool FileMonitor::LoadSubTarget(Node* node, string* err) {
  if (node->monitored())
    return true;

  Edge* edge = node->in_edge();
  if (!edge) {  // Leaf node.
    if (!AddNode(node, err))
      return false;
  }
  else {
    for (vector<Node*>::iterator i = edge->inputs_.begin();
	 i != edge->inputs_.end(); ++i) {
      if ((*i)->dirty())
	continue; // No need to monitor a dirty node.
      if (!LoadSubTarget(*i, err))
	return false;
    }
  }

  node->MarkMonitored();

  return true;
}

bool FileMonitor::AddNode(Node* node, string* err) {

  int wd = inotify_add_watch(inotify_fd_, node->path().c_str(),
    IN_CLOSE_WRITE |
    IN_MODIFY |
    IN_MOVE_SELF |
    IN_DELETE_SELF |
    IN_DELETE);
  if (wd < 0) {
    *err = "error with inotify_add_watch for node ";
    *err += node->path().c_str();
    return false;
  }

  map_fds_[wd] = node;
  map_nodes_[node] = wd;

  return true;
}

bool FileMonitor::Wait()
{
  size_t r;
  char buffer[EVENT_BUF_LEN];
  struct inotify_event* event;

  while (1) {
    FD_ZERO(&inotify_fds_);
    FD_SET(inotify_fd_, &inotify_fds_);

    int ret = select(inotify_fd_ + 1, &inotify_fds_, NULL, NULL, 0);
    if (ret == 0)
      continue;
    if (ret < 0)
    {
      if (errno == EINTR)
	return true; // Not an error, we've been asked to stop.
      return false;
    }

    r = read(inotify_fd_, buffer, EVENT_BUF_LEN);
    if (r <= 0) {
      perror("read");
      return false;
    }

    unsigned int i = 0;
    while (i < r) {
      event = (struct inotify_event*) &buffer[i];
      HandleEvent(event);
      i += EVENT_SIZE + event->len;
    }

    event = (struct inotify_event *) buffer;
    HandleEvent(event);
  }

  return true;
}

bool FileMonitor::HandleEvent(struct inotify_event* event)
{
  int mask = event->mask;

  unordered_map<int, Node*>::iterator i = map_fds_.find(event->wd);
  if (i == map_fds_.end())
  {
    // File descriptor might have been previously removed before this
    // event is dequeued.
    return false;
  }
  Node* node = i->second;

  if (   mask & IN_CLOSE_WRITE
      || mask & IN_MOVE_SELF
      || mask & IN_DELETE_SELF
      || mask & IN_MODIFY)
  {
    string err;
    if (!MarkNodeDirty(node, event->wd, &err))
      Error("Error when marking nodes dirty: %s", err.c_str());
  }
  else if (mask & IN_IGNORED)
  {
    /// TODO: there might be stale wd in the map because we do not remove the
    /// descriptor here.
    string err;
    if (!AddNode(node, &err))
      Error("Error when remonitoring %s: %s", node->path().c_str(), err.c_str());
  }

  if (mask & IN_Q_OVERFLOW)
  {
    /// TODO: in order to be robust, we should go into normal mode when
    /// this happens, meaning we must re-stat all the files before the next
    /// build.
    Warning("Queue overflow");
  }

  return true;
}

/// TODO: When we move the FileMonitor to its own thread, it should have
/// its own queue of file events and it should process them only before
/// the next build. This will prevents many jumps to the main thread so that
/// everything is done in one jump.
bool FileMonitor::MarkNodeDirty(Node* node, int wd, string* err)
{
  if (0 != inotify_rm_watch(inotify_fd_, wd))
  {
    Error("Error while removing watch for %s", node->path().c_str());
    return false;
  }

  map_fds_.erase(wd);
  map_nodes_.erase(node);
  node->set_monitored(false);

  return MarkOutputDirty(node, err);
}

bool FileMonitor::MarkOutputDirty(Node* node, string* err)
{
  if (node->dirty())
    return true;

  node->MarkDirty();
  if (Edge* in_edge = node->in_edge())
  {
    in_edge->outputs_ready_ = false;
  }

  /// Only exploring output edges that are not an order only dependency.
  const vector<Edge*>& out_edges = node->not_order_only_out_edges();
  for (vector<Edge*>::const_iterator e = out_edges.begin();
       e != out_edges.end() && *e != NULL; ++e) {
    Edge* edge = *e;

    for (unsigned int i = 0; i < edge->outputs_.size(); ++i)
    {
      if (!MarkOutputDirty(edge->outputs_[i], err))
	return false;
    }
  }

  return true;
}

bool FileMonitor::MonitorNode(Node* node, string* err)
{
  // This should be a leaf node.
  assert(!node->in_edge() || (node->in_edge()->is_phony()
    && node->in_edge()->inputs_.empty()));
  assert(!node->monitored());

  node->MarkMonitored();

  return AddNode(node, err);
}
