#pragma once

#include <string>

namespace react_native_linux {

/**
 * The clipboard Ctrl+C, Ctrl+X and Ctrl+V move text through, in this process and nowhere else.
 *
 * This is deliberately **not** `wl_data_device` yet, and the reason is the rig rather than the protocol.
 * A system clipboard on Wayland is a data source, a data offer, a MIME negotiation and a file-descriptor
 * transfer, all of which need a compositor and a second client to be worth anything — and the proof rig for the
 * text field is `hello_react`, which has no compositor at all. An in-process clipboard is testable in the unit
 * suite, testable headlessly, and behaviourally identical for everything the editing model can get wrong: what
 * is copied, what a cut leaves behind, what a paste does to a selection, and what a multi-line paste becomes in
 * a single-line field.
 *
 * `wl_data_device` — copy to and paste from other applications, plus the primary selection middle-click paste —
 * is issue #60, and it replaces the body of these two functions and nothing else.
 *
 * Threading contract: the frame thread owns the clipboard, as it owns everything else the input dispatcher
 * reaches. Nothing here is synchronised.
 */
const std::string& clipboardText();
void setClipboardText(std::string text);

} // namespace react_native_linux
