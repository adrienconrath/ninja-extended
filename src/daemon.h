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

#ifndef NINJA_DAEMON_H_
#define NINJA_DAEMON_H_

// TODO: remove includes

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <unistd.h>

#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>

#include "processor.h"
#include "build.h"
#include "build_log.h"
#include "deps_log.h"
#include "clean.h"
#include "debug_flags.h"
#include "disk_interface.h"
#include "graph.h"
#include "graphviz.h"
#include "manifest_parser.h"
#include "file_monitor.h"
#include "comms.h"
#include "metrics.h"
#include "state.h"
#include "util.h"
#include "version.h"

using namespace std;

struct Tool;

/// Command-line options.
struct Options {
  /// Build file to load.
  const char* input_file;

  /// Directory to change into before running.
  const char* working_dir;

  /// Tool to run rather than building.
  const Tool* tool;
};

struct Daemon {
  Daemon(const char* ninja_command, const BuildConfig& config,
    const Options& options);

  /// Main processor.
  Processor processor_;

  /// Command line used to run Ninja.
  const char* ninja_command_;

  /// Build configuration set from flags (e.g. parallelism).
  const BuildConfig& config_;

  const Options& options_;

  /// Loaded state (rules, nodes).
  State state_;

  /// Functions for accesssing the disk.
  RealDiskInterface disk_interface_;

  /// The build directory, used for storing the build log etc.
  string build_dir_;

  BuildLog build_log_;
  DepsLog deps_log_;

  FileMonitor file_monitor_;

  Comms comms_;

  bool continue_;

  /// The type of functions that are the entry points to tools (subcommands).
  typedef int (Daemon::*ToolFunc)(int, char**);

  /// Get the Node for a given command-line path, handling features like
  /// spell correction.
  Node* CollectTarget(const char* cpath, string* err);

  /// CollectTarget for all command-line arguments, filling in \a targets.
  bool CollectTargetsFromArgs(int argc, char* argv[],
                              vector<Node*>* targets, string* err);

  // The various subcommands, run via "-t XXX".
  int ToolGraph(int argc, char* argv[]);
  int ToolQuery(int argc, char* argv[]);
  int ToolDeps(int argc, char* argv[]);
  int ToolMSVC(int argc, char* argv[]);
  int ToolTargets(int argc, char* argv[]);
  int ToolCommands(int argc, char* argv[]);
  int ToolClean(int argc, char* argv[]);
  int ToolCompilationDatabase(int argc, char* argv[]);
  int ToolRecompact(int argc, char* argv[]);
  int ToolUrtle(int argc, char** argv);

  /// Open the build log.
  /// @return false on error.
  bool OpenBuildLog(bool recompact_only = false);

  /// Open the deps log: load it, then open for writing.
  /// @return false on error.
  bool OpenDepsLog(bool recompact_only = false);

  /// Ensure the build directory exists, creating it if necessary.
  /// @return false on error.
  bool EnsureBuildDirExists();

  /// Rebuild the manifest, if necessary.
  /// Fills in \a err on error.
  /// @return true if the manifest was rebuilt.
  bool RebuildManifest(const char* input_file, string* err);

  /// Dump the output requested by '-d stats'.
  void DumpMetrics();

  /// Called when a client has required a build.
  void TriggerBuildMoveToMainThread(const OnBuildCompletedFn& onBuildCompleted);
  void TriggerBuild(const OnBuildCompletedFn& onBuildCompleted);

  /// Called when a client has requested the daemon to stop.
  void TriggerStopMoveToMainThread();
  void TriggerStop();

  void BuildDirtySetMoveToMainThread(const OnBuildCompletedFn& onBuildCompleted,
    const boost::shared_ptr<vector<Node*>>& changed_files);
  bool BuildDirtySet(const OnBuildCompletedFn& onBuildCompleted,
    const boost::shared_ptr<vector<Node*>>& changed_files);
  /// Mark a node and its dependants dirty.
  bool MarkNodeDirty(Node* node, string* err);

  bool RunBuild(const OnBuildCompletedFn& onBuildCompleted);

  /// Load the manifest for the first time and do some preliminary checks.
  bool Load();

  void Run();
};

/// Subtools, accessible via "-t foo".
struct Tool {
  /// Short name of the tool.
  const char* name;

  /// Description (shown in "-t list").
  const char* desc;

  /// When to run the tool.
  enum {
    /// Run after parsing the command-line flags (as early as possible).
    RUN_AFTER_FLAGS,

    /// Run after loading build.ninja.
    RUN_AFTER_LOAD,

    /// Run after loading the build/deps logs.
    RUN_AFTER_LOGS,
  } when;

  /// Implementation of the tool.
  Daemon::ToolFunc func;
};


#endif // NINJA_DAEMON_H_
