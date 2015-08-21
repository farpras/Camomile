/*
  ==============================================================================

    This file was auto-generated by the Introjucer!

    It contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#ifndef PLUGINEDITOR_H_INCLUDED
#define PLUGINEDITOR_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"

class CamomileInterface;

// ==================================================================================== //
//                                  MENU INTERFACE                                      //
// ==================================================================================== //

class MenuInterface : public MenuBarModel
{
private:
    
public:
    StringArray getMenuBarNames() override;
    
    PopupMenu getMenuForIndex(int topLevelMenuIndex, const String& menuName) override;
    
    /** This is called when a menu item has been clicked on.
     
     @param menuItemID           the item ID of the PopupMenu item that was selected
     @param topLevelMenuIndex    the index of the top-level menu from which the item was
     chosen (just in case you've used duplicate ID numbers
     on more than one of the popup menus)
     */
    virtual void menuItemSelected (int menuItemID,
                                   int topLevelMenuIndex) = 0;
};

// ==================================================================================== //
//                                  OBJECT INTERFACE                                    //
// ==================================================================================== //

class ObjectInterface :
public Component,
public Listener,
public TextInputTarget
{
private:
    CamomileInterface& m_interface;
    const wGui       m_object;
    const sMessenger m_messenger;
    bool             m_attached;
public:
    ObjectInterface(CamomileInterface& camo, sGui object);
    virtual ~ObjectInterface() {};
    void paint(Graphics& g) override;
    void mouseMove(const MouseEvent& event) override;
    void mouseEnter(const MouseEvent& event) override;
    void mouseExit(const MouseEvent& event) override;
    void mouseDown(const MouseEvent& event) override;
    void mouseDrag(const MouseEvent& event) override;
    void mouseUp(const MouseEvent& event) override;
    void mouseDoubleClick(const MouseEvent& event) override;
    void mouseWheelMove(const MouseEvent& event, const MouseWheelDetails& wheel) override;
    bool keyPressed(const KeyPress& key) override;
    void receive(const std::string& dest, t_symbol* s);
    
    bool isTextInputActive() const override {return false;}
    Range<int> getHighlightedRegion() const override {return Range<int>();}
    void setHighlightedRegion(const Range<int>& newRange)override {}
    void setTemporaryUnderlining(const Array <Range<int> >& underlinedRegions) override {};
    String getTextInRange(const Range<int>& range) const override {return String();}
    void insertTextAtCaret(const String& textToInsert) override {};
    Rectangle<int> getCaretRectangle() override {return Rectangle<int>();}
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectInterface)
};

// ==================================================================================== //
//                                  CAMOMILE INTERFACE                                  //
// ==================================================================================== //

class CamomileInterface  :
public AudioProcessorEditor,
public CamomileAudioProcessor::Listener,
public FileDragAndDropTarget
{
private:
    CamomileAudioProcessor&     m_processor;
    OwnedArray<ObjectInterface> m_objects;
    bool                        m_dropping;
public:
    CamomileInterface(CamomileAudioProcessor&);
    ~CamomileInterface();
    void paint(Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const StringArray& files) override;
    void filesDropped(const StringArray& files, int x, int y) override;
    void fileDragEnter(const StringArray& files, int x, int y) override;
    void fileDragExit(const StringArray& files) override;
    void patchChanged() override;
    inline void lock() const noexcept {m_processor.lock();}
    inline void unlock() const noexcept {m_processor.unlock();}
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CamomileInterface)
};


#endif  // PLUGINEDITOR_H_INCLUDED
