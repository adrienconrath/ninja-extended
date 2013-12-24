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

#ifdef _WIN32
#error WIN32 not supported yet
#endif

#include "daemon.h"
#include "client.h"

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

/// Print usage information.
void Usage(const BuildConfig& config) {
  fprintf(stderr,
      "usage: ninja [options] [targets...]\n"
      "\n"
      "if targets are unspecified, builds the 'default' target (see manual).\n"
      "\n"
      "options:\n"
      "  --version  print ninja version (\"%s\")\n"
      "\n"
      "  -C DIR   change to DIR before doing anything else\n"
      "  -f FILE  specify input build file [default=build.ninja]\n"
      "\n"
      "  -j N     run N jobs in parallel [default=%d, derived from CPUs available]\n"
      "  -l N     do not start new jobs if the load average is greater than N\n"
      "  -k N     keep going until N jobs fail [default=1]\n"
      "  -n       dry run (don't run commands but act like they succeeded)\n"
      "  -v       show all command lines while building\n"
      "\n"
      "  -d MODE  enable debugging (use -d list to list modes)\n"
      "  -t TOOL  run a subtool (use -t list to list subtools)\n"
      "    terminates toplevel options; further flags are passed to the tool\n",
      kNinjaVersion, config.parallelism);
}

/// Choose a default value for the -j (parallelism) flag.
int GuessParallelism() {
  switch (int processors = GetProcessorCount()) {
    case 0:
    case 1:
      return 2;
    case 2:
      return 3;
    default:
      return processors + 2;
  }
}

/// Find the function to execute for \a tool_name and return it via \a func.
/// Returns a Tool, or NULL if Ninja should exit.
const Tool* ChooseTool(const string& tool_name) {
  static const Tool kTools[] = {
    { "clean", "clean built files",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolClean },
    { "commands", "list all commands required to rebuild given targets",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolCommands },
    { "deps", "show dependencies stored in the deps log",
      Tool::RUN_AFTER_LOGS, &Daemon::ToolDeps },
    { "graph", "output graphviz dot file for targets",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolGraph },
    { "query", "show inputs/outputs for a path",
      Tool::RUN_AFTER_LOGS, &Daemon::ToolQuery },
    { "targets",  "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolTargets },
    { "compdb",  "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolCompilationDatabase },
    { "recompact",  "recompacts ninja-internal data structures",
      Tool::RUN_AFTER_LOAD, &Daemon::ToolRecompact },
    { "urtle", NULL,
      Tool::RUN_AFTER_FLAGS, &Daemon::ToolUrtle },
    { NULL, NULL, Tool::RUN_AFTER_FLAGS, NULL }
  };

  if (tool_name == "list") {
    printf("ninja subtools:\n");
    for (const Tool* tool = &kTools[0]; tool->name; ++tool) {
      if (tool->desc)
        printf("%10s  %s\n", tool->name, tool->desc);
    }
    return NULL;
  }

  for (const Tool* tool = &kTools[0]; tool->name; ++tool) {
    if (tool->name == tool_name)
      return tool;
  }

  vector<const char*> words;
  for (const Tool* tool = &kTools[0]; tool->name; ++tool)
    words.push_back(tool->name);
  const char* suggestion = SpellcheckStringV(tool_name, words);
  if (suggestion) {
    Fatal("unknown tool '%s', did you mean '%s'?",
        tool_name.c_str(), suggestion);
  } else {
    Fatal("unknown tool '%s'", tool_name.c_str());
  }
  return NULL;  // Not reached.
}

/// Enable a debugging mode.  Returns false if Ninja should exit instead
/// of continuing.
bool DebugEnable(const string& name) {
  if (name == "list") {
    printf("debugging modes:\n"
        "  stats    print operation counts/timing info\n"
        "  explain  explain what caused a command to execute\n"
        "  keeprsp  don't delete @response files on success\n"
        "multiple modes can be enabled via -d FOO -d BAR\n");
    return false;
  } else if (name == "stats") {
    g_metrics = new Metrics;
    return true;
  } else if (name == "explain") {
    g_explaining = true;
    return true;
  } else if (name == "keeprsp") {
    g_keep_rsp = true;
    return true;
  } else {
    const char* suggestion =
      SpellcheckString(name.c_str(), "stats", "explain", NULL);
    if (suggestion) {
      Error("unknown debug setting '%s', did you mean '%s'?",
          name.c_str(), suggestion);
    } else {
      Error("unknown debug setting '%s'", name.c_str());
    }
    return false;
  }
}


/// Parse argv for command-line options.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlags(int* argc, char*** argv,
    Options* options, BuildConfig* config) {
  config->parallelism = GuessParallelism();

  enum { OPT_VERSION = 1 };
  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { "version", no_argument, NULL, OPT_VERSION },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  while (!options->tool &&
      (opt = getopt_long(*argc, *argv, "d:f:j:k:l:nt:vC:h", kLongOptions,
                         NULL)) != -1) {
    switch (opt) {
      case 'd':
        if (!DebugEnable(optarg))
          return 1;
        break;
      case 'f':
        options->input_file = optarg;
        break;
      case 'j': {
                  char* end;
                  int value = strtol(optarg, &end, 10);
                  if (*end != 0 || value <= 0)
                    Fatal("invalid -j parameter");
                  config->parallelism = value;
                  break;
                }
      case 'k': {
                  char* end;
                  int value = strtol(optarg, &end, 10);
                  if (*end != 0)
                    Fatal("-k parameter not numeric; did you mean -k 0?");

                  // We want to go until N jobs fail, which means we should allow
                  // N failures and then stop.  For N <= 0, INT_MAX is close enough
                  // to infinite for most sane builds.
                  config->failures_allowed = value > 0 ? value : INT_MAX;
                  break;
                }
      case 'l': {
                  char* end;
                  double value = strtod(optarg, &end);
                  if (end == optarg)
                    Fatal("-l parameter not numeric: did you mean -l 0.0?");
                  config->max_load_average = value;
                  break;
                }
      case 'n':
                config->dry_run = true;
                break;
      case 't':
                options->tool = ChooseTool(optarg);
                if (!options->tool)
                  return 0;
                break;
      case 'v':
                config->verbosity = BuildConfig::VERBOSE;
                break;
      case 'C':
                options->working_dir = optarg;
                break;
      case OPT_VERSION:
                printf("%s\n", kNinjaVersion);
                return 0;
      case 'h':
      default:
                Usage(*config);
                return 1;
    }
  }
  *argv += optind;
  *argc -= optind;

  return -1;
}

// Returns true if the caller is the client.
static bool Daemonize(const char* ninja_command, const BuildConfig& config,
    const Options& options) {

  // We fork twice in order to have the daemon run in its own
  // process group and its own session.
  if (fork()) {
    // The client process continues.
    return true;
  }
  setsid();
  if (fork()) {
    return false;
  }

  Daemon daemon(ninja_command, config, options);

  // Will run indefinitely until the daemon is asked to stop.
  daemon.Run();

  // TODO: here the client still exists, it should be destroyed earlier in the
  // process.
  return false;
}

int real_main(int argc, char** argv) {
  BuildConfig config;
  Options options = {};
  options.input_file = "build.ninja";

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
  const char* ninja_command = argv[0];

  int exit_code = ReadFlags(&argc, &argv, &options, &config);
  if (exit_code >= 0)
    return exit_code;

  /// TODO: we need a protobuf message to ask the daemon to change dir?
  if (options.working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    if (chdir(options.working_dir) < 0) {
      Fatal("chdir to '%s' - %s", options.working_dir, strerror(errno));
    }
  }

  std::unique_ptr<Client> client(new Client("/tmp/ninja-extended"));

  if (!client->Connect()) {
    // Daemon does not exist
    printf("Client: Creating daemon\n");

    // Reset client before calling fork so that it does not continue to exist
    // in the daemon process.
    client.reset(0);
    if (!Daemonize(ninja_command, config, options))
      return 0;

    // Re-create client
    client.reset(new Client("/tmp/ninja-extended"));

    // Trying to reconnect.
    int nb_tries = 0;
    int max_tries = 10;
    bool connected = false;
    do {
      connected = client->Connect();
      nb_tries++;

      if (!connected && nb_tries != max_tries)
        sleep(1);
    }
    while (!connected && nb_tries != max_tries);

    if (!connected)
    {
      Error("Cannot connect to daemon");
      return 1;
    }
  }

  client->Build();

  return 0;
}

int main(int argc, char** argv) {
  return real_main(argc, argv);
}
