// Copyright 2013 the V8 project authors. All rights reserved.
// Rrdistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Rrdistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Rrdistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "src/v8.h"

#include "src/arm64/macro-assembler-arm64-inl.h"
#include "src/base/platform/platform.h"
#include "src/code-stubs.h"
#include "src/factory.h"
#include "src/macro-assembler.h"
#include "src/simulator.h"
#include "test/cctest/cctest.h"
#include "test/cctest/test-code-stubs.h"

namespace v8 {
namespace internal {

#define __ masm.

ConvertDToIFunc MakeConvertDToIFuncTrampoline(Isolate* isolate,
                                              Register destination_reg) {
  // Allocate an executable page of memory.
  size_t actual_size = 4 * Assembler::kMinimalBufferSize;
  byte* buffer = static_cast<byte*>(
      v8::base::OS::Allocate(actual_size, &actual_size, true));
  CHECK(buffer);
  HandleScope handles(isolate);
  MacroAssembler masm(isolate, buffer, static_cast<int>(actual_size),
                      v8::internal::CodeObjectRequired::kYes);
  DoubleToIStub stub(isolate, destination_reg);

  byte* start = stub.GetCode()->instruction_start();

  __ SetStackPointer(csp);
  __ PushCalleeSavedRegisters();
  __ Mov(jssp, csp);
  __ SetStackPointer(jssp);

  // Push the double argument.
  __ Push(d0);

  MacroAssembler::PushPopQueue queue(&masm);

  // Save registers make sure they don't get clobbered.
  int source_reg_offset = kDoubleSize;
  int reg_num = 0;
  for (; reg_num < Register::kNumRegisters; ++reg_num) {
    if (RegisterConfiguration::Default()->IsAllocatableGeneralCode(reg_num)) {
      Register reg = Register::from_code(reg_num);
      if (!reg.is(destination_reg)) {
        queue.Queue(reg);
        source_reg_offset += kPointerSize;
      }
    }
  }
  // Re-push the double argument.
  queue.Queue(d0);

  queue.PushQueued();

  // Call through to the actual stub
  __ Call(start, RelocInfo::EXTERNAL_REFERENCE);

  __ Drop(1, kDoubleSize);

  // // Make sure no registers have been unexpectedly clobbered
  for (--reg_num; reg_num >= 0; --reg_num) {
    if (RegisterConfiguration::Default()->IsAllocatableGeneralCode(reg_num)) {
      Register reg = Register::from_code(reg_num);
      if (!reg.is(destination_reg)) {
        __ Pop(ip0);
        __ cmp(reg, ip0);
        __ Assert(eq, kRegisterWasClobbered);
      }
    }
  }

  __ Drop(1, kDoubleSize);

  if (!destination_reg.is(x0))
    __ Mov(x0, destination_reg);

  // Restore callee save registers.
  __ Mov(csp, jssp);
  __ SetStackPointer(csp);
  __ PopCalleeSavedRegisters();

  __ Ret();

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Assembler::FlushICache(isolate, buffer, actual_size);
  return (reinterpret_cast<ConvertDToIFunc>(
      reinterpret_cast<intptr_t>(buffer)));
}

#undef __


static Isolate* GetIsolateFrom(LocalContext* context) {
  return reinterpret_cast<Isolate*>((*context)->GetIsolate());
}


int32_t RunGeneratedCodeCallWrapper(ConvertDToIFunc func,
                                    double from) {
#ifdef USE_SIMULATOR
  Simulator::CallArgument args[] = {
      Simulator::CallArgument(from),
      Simulator::CallArgument::End()
  };
  return static_cast<int32_t>(Simulator::current(CcTest::i_isolate())
                                  ->CallInt64(FUNCTION_ADDR(func), args));
#else
  return (*func)(from);
#endif
}


TEST(ConvertDToI) {
  CcTest::InitializeVM();
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

#if DEBUG
  // Verify that the tests actually work with the C version. In the release
  // code, the compiler optimizes it away because it's all constant, but does it
  // wrong, triggering an assert on gcc.
  RunAllTruncationTests(&ConvertDToICVersion);
#endif

  Register dest_registers[] = {x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11,
                               x12, x13, x14, x15, x18, x19, x20, x21, x22, x23,
                               x24};

  for (size_t d = 0; d < sizeof(dest_registers) / sizeof(Register); d++) {
    RunAllTruncationTests(
        RunGeneratedCodeCallWrapper,
        MakeConvertDToIFuncTrampoline(isolate, dest_registers[d]));
  }
}

}  // namespace internal
}  // namespace v8
