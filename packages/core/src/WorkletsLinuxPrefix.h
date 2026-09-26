#pragma once

// The prefix header rnl_worklets compiles every react-native-worklets source with (#134), the Linux counterpart of
// the package's android/src/main/cpp/WorkletsPCH.h. Common/cpp relies on that precompiled header for includes it
// does not spell itself — <algorithm> for JSISerializer.cpp's std::find, <stdexcept>, which fbjni pulls in on
// Android, for FeatureFlags.h's std::logic_error, and <iterator> for WorkletsJSIUtils.cpp's std::istream_iterator —
// so the same list is here, minus fbjni.

#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <jsi/jsi.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// RunLoop/AsyncQueueImpl.cpp treats every platform that is not Android as Apple, whose pthread_setname_np names
// the calling thread and takes one argument. glibc's takes the thread as well; this is Apple's form over it.
inline int pthread_setname_np(const char* name) { return pthread_setname_np(pthread_self(), name); }
