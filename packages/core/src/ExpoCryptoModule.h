#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoCrypto` (ADR-0004, #276), which upstream's unmodified `expo-crypto` requires as it is imported, over the
 * primitives in `ExpoCrypto.h`. Its methods are the ones T3 Code reaches: `digest` writes into the `output` typed
 * array as Android does, at most its length; `digestStringAsync` resolves the encoded digest of a string's UTF-8
 * bytes; `getRandomValues` fills a typed array in place and returns it, as iOS does; `randomUUID` answers a version 4
 * UUID. `digestString` and `digestAsync` are absent, and upstream falls back from the second to `digest`.
 *
 * Threading contract: every method runs on the JavaScript thread, and `digestStringAsync` settles its promise there
 * before it returns.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoCryptoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

/**
 * `ExpoCryptoAES`, which `expo-crypto`'s entry also requires as it is imported, because its `AESEncryptionKey` and
 * `AESSealedData` extend the module's `EncryptionKey` and `SealedData` classes at module scope. T3 Code calls none of
 * AES, so the classes come from `makeSharedObjectClass` with no members and throw when constructed, and the module
 * has no methods, so a call to one fails as a call to an undefined function.
 *
 * Threading contract: the JavaScript thread only.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoCryptoAESModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
