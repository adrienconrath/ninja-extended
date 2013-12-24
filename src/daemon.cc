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

#include "daemon.h"

/// An implementation of ManifestParser::FileReader that actually reads
/// the file.
struct RealFileReader : public ManifestParser::FileReader {
  virtual bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

void PrintCommands(Edge* edge, set<Edge*>* seen) {
  if (!edge)
    return;
  if (!seen->insert(edge).second)
    return;

  for (vector<Node*>::iterator in = edge->inputs_.begin();
       in != edge->inputs_.end(); ++in)
    PrintCommands((*in)->in_edge(), seen);

  if (!edge->is_phony())
    puts(edge->EvaluateCommand().c_str());
}

void EncodeJSONString(const char *str) {
  while (*str) {
    if (*str == '"' || *str == '\\')
      putchar('\\');
    putchar(*str);
    str++;
  }
}

int ToolTargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        ToolTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int ToolTargetsSourceList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
         inps != (*e)->inputs_.end(); ++inps) {
      if (!(*inps)->in_edge())
        printf("%s\n", (*inps)->path().c_str());
    }
  }
  return 0;
}

int ToolTargetsList(State* state, const string& rule_name) {
  set<string> rules;

  // Gather the outputs.
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    if ((*e)->rule_->name() == rule_name) {
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end(); ++out_node) {
        rules.insert((*out_node)->path());
      }
    }
  }

  // Print them.
  for (set<string>::const_iterator i = rules.begin();
       i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int ToolTargetsList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      printf("%s: %s\n",
             (*out_node)->path().c_str(),
             (*e)->rule_->name().c_str());
    }
  }
  return 0;
}


Daemon::Daemon(const char* ninja_command, const BuildConfig& config,
    const Options& options)
  : ninja_command_(ninja_command), config_(config), options_(options),
  file_monitor_(&state_), comms_("/tmp/ninja-extended"), continue_(true) {
    // Hook up the build command to its handler.
    comms_.SetOnBuildCmdFn(
        boost::bind(&Daemon::TriggerBuildMoveToMainThread, this, _1));

    // Hook up the stop command to its handler.
    comms_.SetOnStopCmdFn(
        boost::bind(&Daemon::TriggerStopMoveToMainThread, this));

    Load();
  }

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool Daemon::RebuildManifest(const char* input_file, string* err) {
  string path = input_file;
  if (!CanonicalizePath(&path, err))
    return false;
  Node* node = state_.LookupNode(path);
  if (!node)
    return false;

  Builder builder(&state_, config_, &build_log_, &deps_log_, &disk_interface_,
      &file_monitor_);
  if (!builder.AddTarget(node, err))
    return false;

  if (builder.AlreadyUpToDate())
    return false;  // Not an error, but we didn't rebuild.
  if (!builder.Build(err))
    return false;

  // The manifest was only rebuilt if it is now dirty (it may have been cleaned
  // by a restat).
  return node->dirty();
}

Node* Daemon::CollectTarget(const char* cpath, string* err) {
  string path = cpath;
  if (!CanonicalizePath(&path, err))
    return NULL;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!path.empty() && path[path.size() - 1] == '^') {
    path.resize(path.size() - 1);
    first_dependent = true;
  }

  Node* node = state_.LookupNode(path);
  if (node) {
    if (first_dependent) {
      if (node->out_edges().empty()) {
        *err = "'" + path + "' has no out edge";
        return NULL;
      }
      Edge* edge = node->out_edges()[0];
      if (edge->outputs_.empty()) {
        edge->Dump();
        Fatal("edge has no outputs");
      }
      node = edge->outputs_[0];
    }
    return node;
  } else {
    *err = "unknown target '" + path + "'";

    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    } else {
      Node* suggestion = state_.SpellcheckNode(path);
      if (suggestion) {
        *err += ", did you mean '" + suggestion->path() + "'?";
      }
    }
    return NULL;
  }
}

bool Daemon::CollectTargetsFromArgs(int argc, char* argv[],
    vector<Node*>* targets, string* err) {
  if (argc == 0) {
    *targets = state_.DefaultNodes(err);
    return err->empty();
  }

  for (int i = 0; i < argc; ++i) {
    Node* node = CollectTarget(argv[i], err);
    if (node == NULL)
      return false;
    targets->push_back(node);
  }
  return true;
}

int Daemon::ToolGraph(int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  GraphViz graph;
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int Daemon::ToolQuery(int argc, char* argv[]) {
  if (argc == 0) {
    Error("expected a target to query");
    return 1;
  }

  for (int i = 0; i < argc; ++i) {
    string err;
    Node* node = CollectTarget(argv[i], &err);
    if (!node) {
      Error("%s", err.c_str());
      return 1;
    }

    printf("%s:\n", node->path().c_str());
    if (Edge* edge = node->in_edge()) {
      printf("  input: %s\n", edge->rule_->name().c_str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char* label = "";
        if (edge->is_implicit(in))
          label = "| ";
        else if (edge->is_order_only(in))
          label = "|| ";
        printf("    %s%s\n", label, edge->inputs_[in]->path().c_str());
      }
    }
    printf("  outputs:\n");
    for (vector<Edge*>::const_iterator edge = node->out_edges().begin();
        edge != node->out_edges().end(); ++edge) {
      for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
          out != (*edge)->outputs_.end(); ++out) {
        printf("    %s\n", (*out)->path().c_str());
      }
    }
  }
  return 0;
}

int Daemon::ToolDeps(int argc, char** argv) {
  vector<Node*> nodes;
  if (argc == 0) {
    for (vector<Node*>::const_iterator ni = deps_log_.nodes().begin();
        ni != deps_log_.nodes().end(); ++ni) {
      // Only query for targets with an incoming edge and deps
      Edge* e = (*ni)->in_edge();
      if (e && !e->GetBinding("deps").empty())
        nodes.push_back(*ni);
    }
  } else {
    string err;
    if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
      Error("%s", err.c_str());
      return 1;
    }
  }

  RealDiskInterface disk_interface;
  for (vector<Node*>::iterator it = nodes.begin(), end = nodes.end();
      it != end; ++it) {
    DepsLog::Deps* deps = deps_log_.GetDeps(*it);
    if (!deps) {
      printf("%s: deps not found\n", (*it)->path().c_str());
      continue;
    }

    TimeStamp mtime = disk_interface.Stat((*it)->path());
    printf("%s: #deps %d, deps mtime %d (%s)\n",
        (*it)->path().c_str(), deps->node_count, deps->mtime,
        (!mtime || mtime > deps->mtime ? "STALE":"VALID"));
    for (int i = 0; i < deps->node_count; ++i)
      printf("    %s\n", deps->nodes[i]->path().c_str());
    printf("\n");
  }

  return 0;
}

int Daemon::ToolTargets(int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return ToolTargetsSourceList(&state_);
      else
        return ToolTargetsList(&state_, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return ToolTargetsList(&state_);
    } else {
      const char* suggestion =
        SpellcheckString(mode.c_str(), "rule", "depth", "all", NULL);
      if (suggestion) {
        Error("unknown target tool mode '%s', did you mean '%s'?",
            mode.c_str(), suggestion);
      } else {
        Error("unknown target tool mode '%s'", mode.c_str());
      }
      return 1;
    }
  }

  string err;
  vector<Node*> root_nodes = state_.RootNodes(&err);
  if (err.empty()) {
    return ToolTargetsList(root_nodes, depth, 0);
  } else {
    Error("%s", err.c_str());
    return 1;
  }
}

int Daemon::ToolCommands(int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  set<Edge*> seen;
  for (vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen);

  return 0;
}

int Daemon::ToolClean(int argc, char* argv[]) {
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "clean".
  argc++;
  argv--;

  bool generator = false;
  bool clean_rules = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hgr"))) != -1) {
    switch (opt) {
      case 'g':
        generator = true;
        break;
      case 'r':
        clean_rules = true;
        break;
      case 'h':
      default:
        printf("usage: ninja -t clean [options] [targets]\n"
            "\n"
            "options:\n"
            "  -g     also clean files marked as ninja generator output\n"
            "  -r     interpret targets as a list of rules to clean instead\n"
            );
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (clean_rules && argc == 0) {
    Error("expected a rule to clean");
    return 1;
  }

  Cleaner cleaner(&state_, config_);
  if (argc >= 1) {
    if (clean_rules)
      return cleaner.CleanRules(argc, argv);
    else
      return cleaner.CleanTargets(argc, argv);
  } else {
    return cleaner.CleanAll(generator);
  }
}

int Daemon::ToolCompilationDatabase(int argc, char* argv[]) {
  bool first = true;
  vector<char> cwd;

  do {
    cwd.resize(cwd.size() + 1024);
    errno = 0;
  } while (!getcwd(&cwd[0], cwd.size()) && errno == ERANGE);
  if (errno != 0 && errno != ERANGE) {
    Error("cannot determine working directory: %s", strerror(errno));
    return 1;
  }

  putchar('[');
  for (vector<Edge*>::iterator e = state_.edges_.begin();
      e != state_.edges_.end(); ++e) {
    for (int i = 0; i != argc; ++i) {
      if ((*e)->rule_->name() == argv[i]) {
        if (!first)
          putchar(',');

        printf("\n  {\n    \"directory\": \"");
        EncodeJSONString(&cwd[0]);
        printf("\",\n    \"command\": \"");
        EncodeJSONString((*e)->EvaluateCommand().c_str());
        printf("\",\n    \"file\": \"");
        EncodeJSONString((*e)->inputs_[0]->path().c_str());
        printf("\"\n  }");

        first = false;
      }
    }
  }

  puts("\n]");
  return 0;
}

int Daemon::ToolRecompact(int argc, char* argv[]) {
  if (!EnsureBuildDirExists())
    return 1;

  if (!OpenBuildLog(/*recompact_only=*/true) ||
      !OpenDepsLog(/*recompact_only=*/true))
    return 1;

  return 0;
}

int Daemon::ToolUrtle(int argc, char** argv) {
  // RLE encoded.
  const char* urtle =
    " 13 ,3;2!2;\n8 ,;<11!;\n5 `'<10!(2`'2!\n11 ,6;, `\\. `\\9 .,c13$ec,.\n6 "
    ",2;11!>; `. ,;!2> .e8$2\".2 \"?7$e.\n <:<8!'` 2.3,.2` ,3!' ;,(?7\";2!2'<"
    "; `?6$PF ,;,\n2 `'4!8;<!3'`2 3! ;,`'2`2'3!;4!`2.`!;2 3,2 .<!2'`).\n5 3`5"
    "'2`9 `!2 `4!><3;5! J2$b,`!>;2!:2!`,d?b`!>\n26 `'-;,(<9!> $F3 )3.:!.2 d\""
    "2 ) !>\n30 7`2'<3!- \"=-='5 .2 `2-=\",!>\n25 .ze9$er2 .,cd16$bc.'\n22 .e"
    "14$,26$.\n21 z45$c .\n20 J50$c\n20 14$P\"`?34$b\n20 14$ dbc `2\"?22$?7$c"
    "\n20 ?18$c.6 4\"8?4\" c8$P\n9 .2,.8 \"20$c.3 ._14 J9$\n .2,2c9$bec,.2 `?"
    "21$c.3`4%,3%,3 c8$P\"\n22$c2 2\"?21$bc2,.2` .2,c7$P2\",cb\n23$b bc,.2\"2"
    "?14$2F2\"5?2\",J5$P\" ,zd3$\n24$ ?$3?%3 `2\"2?12$bcucd3$P3\"2 2=7$\n23$P"
    "\" ,3;<5!>2;,. `4\"6?2\"2 ,9;, `\"?2$\n";
  int count = 0;
  for (const char* p = urtle; *p; p++) {
    if ('0' <= *p && *p <= '9') {
      count = count*10 + *p - '0';
    } else {
      for (int i = 0; i < std::max(count, 1); ++i)
        printf("%c", *p);
      count = 0;
    }
  }
  return 0;
}

bool Daemon::OpenBuildLog(bool recompact_only) {
  string log_path = ".ninja_log";
  if (!build_dir_.empty())
    log_path = build_dir_ + "/" + log_path;

  string err;
  if (!build_log_.Load(log_path, &err)) {
    Error("loading build log %s: %s", log_path.c_str(), err.c_str());
    return false;
  }
  if (!err.empty()) {
    // Hack: Load() can return a warning via err by returning true.
    Warning("%s", err.c_str());
    err.clear();
  }

  if (recompact_only) {
    bool success = build_log_.Recompact(log_path, &err);
    if (!success)
      Error("failed recompaction: %s", err.c_str());
    return success;
  }

  if (!config_.dry_run) {
    if (!build_log_.OpenForWrite(log_path, &err)) {
      Error("opening build log: %s", err.c_str());
      return false;
    }
  }

  return true;
}

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool Daemon::OpenDepsLog(bool recompact_only) {
  string path = ".ninja_deps";
  if (!build_dir_.empty())
    path = build_dir_ + "/" + path;

  string err;
  if (!deps_log_.Load(path, &state_, &err)) {
    Error("loading deps log %s: %s", path.c_str(), err.c_str());
    return false;
  }
  if (!err.empty()) {
    // Hack: Load() can return a warning via err by returning true.
    Warning("%s", err.c_str());
    err.clear();
  }

  if (recompact_only) {
    bool success = deps_log_.Recompact(path, &err);
    if (!success)
      Error("failed recompaction: %s", err.c_str());
    return success;
  }

  if (!config_.dry_run) {
    if (!deps_log_.OpenForWrite(path, &err)) {
      Error("opening deps log: %s", err.c_str());
      return false;
    }
  }

  return true;
}

void Daemon::DumpMetrics() {
  g_metrics->Report();

  printf("\n");
  int count = (int)state_.paths_.size();
  int buckets = (int)state_.paths_.bucket_count();
  printf("path->node hash load %.2f (%d entries / %d buckets)\n",
      count / (double) buckets, count, buckets);
}

bool Daemon::EnsureBuildDirExists() {
  build_dir_ = state_.bindings_.LookupVariable("builddir");
  if (!build_dir_.empty() && !config_.dry_run) {
    if (!disk_interface_.MakeDirs(build_dir_ + "/.") && errno != EEXIST) {
      Error("creating build directory %s: %s",
          build_dir_.c_str(), strerror(errno));
      return false;
    }
  }
  return true;
}

void Daemon::TriggerBuildMoveToMainThread(
    const OnBuildCompletedFn& onBuildCompleted) {
  processor_.Post(boost::bind(&Daemon::TriggerBuild, this, onBuildCompleted));
}

void Daemon::TriggerStopMoveToMainThread() {
  processor_.Post(boost::bind(&Daemon::TriggerStop, this));
}

void Daemon::TriggerBuild(const OnBuildCompletedFn& onBuildCompleted) {
  // Flush the file monitor so that we know what to rebuild.
  file_monitor_.Flush(boost::bind(
        &Daemon::BuildDirtySetMoveToMainThread, this, onBuildCompleted, _1));
}

void Daemon::TriggerStop() {
  // This will cause the processor to be stopped and the process to exit.
  continue_ = false;
}

void Daemon::BuildDirtySetMoveToMainThread(const OnBuildCompletedFn& onBuildCompleted,
    const boost::shared_ptr<vector<Node*>>& changed_files) {
  processor_.Post(boost::bind(
        &Daemon::BuildDirtySet, this, onBuildCompleted, changed_files));
}

bool Daemon::BuildDirtySet(const OnBuildCompletedFn& onBuildCompleted,
    const boost::shared_ptr<vector<Node*>>& changed_files) {
  for (vector<Node*>::iterator it = changed_files->begin();
      it != changed_files->end(); ++it)
  {
    string err;
    if (!MarkNodeDirty(*it, &err))
      Error("Error while marking node dirty: %s", (*it)->path().c_str());
  }

  string err;
  if (RebuildManifest(options_.input_file, &err)) {
    // Not handled yet.
    Error("Manifest has changed, this is not implemented yet.");
    return false;
  } else if (!err.empty()) {
    Error("rebuilding '%s': %s", options_.input_file, err.c_str());
    return false;
  }

  bool res = RunBuild(onBuildCompleted);
  if (g_metrics)
    DumpMetrics(); /// TODO: metrics should be reset after a build.

  return res;
}

bool Daemon::MarkNodeDirty(Node* node, string* err)
{
  node->MarkDirty();
  if (Edge* in_edge = node->in_edge())
  {
    if (!in_edge->outputs_ready_)
      return true;
    in_edge->outputs_ready_ = false;
  }

  /// Only exploring output edges that are not an order only dependency.
  const vector<Edge*>& out_edges = node->not_order_only_out_edges();
  for (vector<Edge*>::const_iterator e = out_edges.begin();
      e != out_edges.end() && *e != NULL; ++e) {
    Edge* edge = *e;

    for (unsigned int i = 0; i < edge->outputs_.size(); ++i)
    {
      if (!MarkNodeDirty(edge->outputs_[i], err))
        return false;
    }
  }

  return true;
}

bool Daemon::Load() {
  RealFileReader file_reader;
  ManifestParser parser(&state_, &file_reader);
  string err;
  if (!parser.Load(options_.input_file, &err)) {
    Error("%s", err.c_str());
    return false;
  }

#if 0
  if (options_.tool && options.tool->when == Tool::RUN_AFTER_LOAD)
    return (*options_.tool->func)(argc, argv);
#endif

  if (!EnsureBuildDirExists())
    return false;

  if (!OpenBuildLog() || !OpenDepsLog())
    return false;

#if 0
  if (options_.tool && options.tool->when == Tool::RUN_AFTER_LOGS)
    return (*options.tool->func)(argc, argv);
#endif

  if (!file_monitor_.Start(&err)) {
    Error("%s", err.c_str());
    return false;
  }

  return true;
}

void Daemon::Run() {
  while (continue_) {
    processor_.RunOne();
  }
}

bool Daemon::RunBuild(const OnBuildCompletedFn& onBuildCompleted) {
  Node* node = state_.LookupNode("all");
  assert(node);

  Builder builder(&state_, config_, &build_log_, &deps_log_, &disk_interface_,
    &file_monitor_);

  std::string err;
  if (!builder.AddTarget(node, &err)) {
    if (!err.empty()) {
      Error("%s", err.c_str());
      return false;
    } else {
      // Added a target that is already up-to-date; not really
      // an error.
    }
  }

  if (builder.AlreadyUpToDate()) {
    printf("ninja: no work to do.\n");
    onBuildCompleted();
    return true;
  }

  if (!builder.Build(&err)) {
    printf("ninja: build stopped: %s.\n", err.c_str());
    if (err.find("interrupted by user") != string::npos) {
      onBuildCompleted();
      return false;
    }
    else {
      onBuildCompleted();
      return false;
    }
  }

  onBuildCompleted();
  return true;
}
