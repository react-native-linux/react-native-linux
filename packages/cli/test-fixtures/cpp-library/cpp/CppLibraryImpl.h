#pragma once

#include <CppLibrarySpecJSI.h>

#include <memory>

namespace facebook::react {

class CppLibraryImpl
  : public NativeCppLibraryCxxSpec<CppLibraryImpl> {
public:
  CppLibraryImpl(std::shared_ptr<CallInvoker> jsInvoker);

  double multiply(jsi::Runtime& rt, double a, double b);
};

}
