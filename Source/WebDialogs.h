// The page's alert / confirm / prompt (deleting a preset, saving over one, the settings password, typing a CC):
// JUCE's WKWebView delegate doesn't answer them, so they'd return false / null. addWebDialogs() gives that delegate
// class native NSAlert versions. Call it with the editor's top NSView once the web view is up; true once done.
#pragma once
bool addWebDialogs(void* nsView);
