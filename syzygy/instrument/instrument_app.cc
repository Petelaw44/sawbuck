// Copyright 2012 Google Inc.
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
//
// Defines the InstrumentApp class, which implements the command-line
// "instrument" tool.

#include "syzygy/instrument/instrument_app.h"

#include <algorithm>
#include <iostream>

#include "base/string_util.h"
#include "syzygy/instrument/transforms/entry_thunk_transform.h"
#include "syzygy/pe/pe_relinker.h"

namespace instrument {

namespace {

using block_graph::BlockGraph;
using instrument::Instrumenter;
using pe::Decomposer;
using pe::PEFile;

static const char kUsageFormatStr[] =
    "Usage: %ls [options]\n"
    "  Required Options:\n"
    "    --input-dll=<path>  The input DLL to instrument.\n"
    "    --output-dll=<path> The instrumented output DLL.\n"
    "\n"
    "  Options:\n"
    "    --call-trace-client=ETW|RPC|PROFILER|<other.dll>\n"
    "                        The call-trace client DLL to reference in the\n"
    "                        instrumented binary. The default value is ETW,\n"
    "                        which maps to the ETW based call-trace client.\n"
    "                        The value RPC maps to the RPC based call-trace\n"
    "                        client. The value PROFILER maps to the profiler\n"
    "                        client. You may also specify the name of any\n"
    "                        DLL which implements the call trace client\n"
    "                        interface.\n"
    "    --debug-friendly    Generate more debugger friendly output by making\n"
    "                        the thunks resolve to the original function's\n"
    "                        name. This is at the cost of the uniqueness of\n"
    "                        address->name resolution.\n"
    "    --input-pdb=<path>  The PDB for the DLL to instrument. If not\n"
    "                        explicitly provided will be searched for.\n"
    "    --no-interior-refs  Perform no instrumentation of references to non-\n"
    "                        zero offsets in code blocks. Implicit when\n"
    "                        --call-trace-client=PROFILER is specified.\n"
    "    --output-pdb=<path> The PDB for the instrumented DLL. If not\n"
    "                        provided will attempt to generate one.\n"
    "    --overwrite         Allow output files to be overwritten.\n"
    "\n";

}  // namespace

pe::PERelinker& InstrumentApp::GetRelinker() {
  if (relinker_.get() == NULL) {
    relinker_.reset(new pe::PERelinker());
    CHECK(relinker_.get() != NULL);
  }
  return *(relinker_.get());
}

Instrumenter& InstrumentApp::GetInstrumenter() {
  if (instrumenter_.get() == NULL) {
    instrumenter_.reset(new Instrumenter());
    CHECK(instrumenter_.get() != NULL);
  }
  return *(instrumenter_.get());
}

bool InstrumentApp::ParseCommandLine(const CommandLine* cmd_line) {
  DCHECK(cmd_line != NULL);

  if (cmd_line->HasSwitch("help"))
    return Usage(cmd_line, "");

  input_dll_path_ = cmd_line->GetSwitchValuePath("input-dll");
  input_pdb_path_ = cmd_line->GetSwitchValuePath("input-pdb");
  output_dll_path_ = cmd_line->GetSwitchValuePath("output-dll");
  output_pdb_path_ = cmd_line->GetSwitchValuePath("output-pdb");
  client_dll_ = cmd_line->GetSwitchValueASCII("call-trace-client");
  instrument_interior_references_ = !cmd_line->HasSwitch("no-interior-refs");
  allow_overwrite_ = cmd_line->HasSwitch("overwrite");
  debug_friendly_ = cmd_line->HasSwitch("debug-friendly");

  if (input_dll_path_.empty() || output_dll_path_.empty())
    return Usage(cmd_line, "You must provide input and output file names.");

  if (client_dll_.empty() || LowerCaseEqualsASCII(client_dll_, "etw")) {
    client_dll_ = Instrumenter::kCallTraceClientDllEtw;
  } else if (LowerCaseEqualsASCII(client_dll_, "rpc")) {
    client_dll_ = Instrumenter::kCallTraceClientDllRpc;
  } else if (LowerCaseEqualsASCII(client_dll_, "profiler")) {
    client_dll_ = Instrumenter::kCallTraceClientDllProfiler;
    instrument_interior_references_ = false;
  }

  return true;
}

int InstrumentApp::Run() {
  pe::PERelinker& relinker = GetRelinker();
  relinker.set_input_path(input_dll_path_);
  relinker.set_input_pdb_path(input_pdb_path_);
  relinker.set_output_path(output_dll_path_);
  relinker.set_output_pdb_path(output_pdb_path_);
  relinker.set_allow_overwrite(allow_overwrite_);

  // Initialize the relinker. This does the decomposition, etc.
  if (!relinker.Init()) {
    LOG(ERROR) << "Failed to initialize relinker.";
    return 1;
  }

  // Set up the instrumenting transform and add it to the relinker.
  instrument::transforms::EntryThunkTransform entry_thunk_tx;
  entry_thunk_tx.set_instrument_dll_name(client_dll_);
  entry_thunk_tx.set_instrument_interior_references(
      instrument_interior_references_);
  entry_thunk_tx.set_src_ranges_for_thunks(debug_friendly_);
  relinker.AppendTransform(&entry_thunk_tx);

  // We let the PERelinker use the implicit OriginalOrderer.
  if (!relinker.Relink()) {
    LOG(ERROR) << "Unable to relink input image.";
    return 1;
  }

  return 0;
}

bool InstrumentApp::Usage(const CommandLine* cmd_line,
                          const base::StringPiece& message) const {
  if (!message.empty()) {
    ::fwrite(message.data(), 1, message.length(), err());
    ::fprintf(err(), "\n\n");
  }

  ::fprintf(err(),
            kUsageFormatStr,
            cmd_line->GetProgram().BaseName().value().c_str());

  return false;
}

}  // namespace instrument