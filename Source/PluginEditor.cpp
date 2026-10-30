#include "PluginEditor.h"
#include "WebHost.h"

ElixirEditor::ElixirEditor(ElixirProcessor& p) : AudioProcessorEditor(p), proc(p) {
    addAndMakeVisible(proc.web().view());
    setResizable(true, true);
    setResizeLimits(1000, 620, 2560, 1600);
    setSize(1280, 780);
}
ElixirEditor::~ElixirEditor() {
    removeChildComponent(&proc.web().view());
    proc.web().view().setVisible(true);
}
void ElixirEditor::resized() { proc.web().view().setBounds(getLocalBounds()); }
