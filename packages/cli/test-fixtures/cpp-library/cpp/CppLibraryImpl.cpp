#include "CppLibraryImpl.h"

namespace facebook::react {

CppLibraryImpl::CppLibraryImpl(
  std::shared_ptr<CallInvoker> jsInvoker
)
  : NativeCppLibraryCxxSpec(std::move(jsInvoker)) {}

double CppLibraryImpl::multiply(
  jsi::Runtime& rt,
  double a,
  double b
) {
  return a * b;
}

}
