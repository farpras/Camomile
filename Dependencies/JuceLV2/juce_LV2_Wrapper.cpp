/*
  ==============================================================================

   Juce LV2 Wrapper

  ==============================================================================
*/

#if JucePlugin_Build_LV2

#include "AppConfig.h"
#include "../../Juce/modules/juce_core/system/juce_TargetPlatform.h"
#include "../../Juce/modules/juce_audio_plugin_client/utility/juce_CheckSettingMacros.h"
#include "../../Juce/modules/juce_audio_plugin_client/utility/juce_IncludeModuleHeaders.h"
#include <cmath>

/** States are strings, needs custom get/setStateInformationString */
#ifndef JucePlugin_WantsLV2StateString
 #define JucePlugin_WantsLV2StateString 0
#endif

/** Using string states require enabling states first */
#if JucePlugin_WantsLV2StateString && ! JucePlugin_WantsLV2State
 #undef JucePlugin_WantsLV2State
 #define JucePlugin_WantsLV2State 1
#endif

#if JUCE_LINUX
 #include <X11/Xlib.h>
 #undef KeyPress
#endif

// LV2 includes..
#include "includes/lv2.h"
#include "includes/atom.h"
#include "includes/atom-util.h"
#include "includes/buf-size.h"
#include "includes/instance-access.h"
#include "includes/midi.h"
#include "includes/options.h"
#include "includes/port-props.h"
#include "includes/presets.h"
#include "includes/state.h"
#include "includes/time.h"
#include "includes/ui.h"
#include "includes/urid.h"
#include "includes/lv2_external_ui.h"
#include "includes/lv2_programs.h"

#define JUCE_LV2_STATE_STRING_URI "urn:juce:stateString"
#define JUCE_LV2_STATE_BINARY_URI "urn:juce:stateBinary"

//==============================================================================
/**
    Lightweight DocumentWindow subclass for external ui
*/
class JuceLv2ExternalUIWindow : public juce::DocumentWindow
{
public:
    /** Creates a Document Window wrapper */
    JuceLv2ExternalUIWindow (juce::AudioProcessorEditor* editor, const juce::String& title) :
            DocumentWindow (title, juce::Colours::white, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton, false),
            closed (false),
            lastPos (0, 0)
    {
        setOpaque (true);
        setContentNonOwned (editor, true);
        setSize (editor->getWidth(), editor->getHeight());
        setUsingNativeTitleBar (true);
    }

    /** Close button handler */
    void closeButtonPressed()
    {
        saveLastPos();
        removeFromDesktop();
        closed = true;
    }

    void saveLastPos()
    {
        lastPos = getScreenPosition();
    }

    void restoreLastPos()
    {
        setTopLeftPosition (lastPos.getX(), lastPos.getY());
    }

    Point<int> getLastPos()
    {
        return lastPos;
    }

    bool isClosed()
    {
        return closed;
    }

    void reset()
    {
        closed = false;
    }

private:
    bool closed;
    Point<int> lastPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceLv2ExternalUIWindow);
};

//==============================================================================
/**
    Juce LV2 External UI handle
*/
class JuceLv2ExternalUIWrapper : public LV2_External_UI_Widget
{
public:
    JuceLv2ExternalUIWrapper (juce::AudioProcessorEditor* editor, const juce::String& title)
        : window (editor, title)
    {
        // external UI calls
        run  = doRun;
        show = doShow;
        hide = doHide;
    }

    ~JuceLv2ExternalUIWrapper()
    {
        if (window.isOnDesktop())
            window.removeFromDesktop();
    }

    void close()
    {
        window.closeButtonPressed();
    }

    bool isClosed()
    {
        return window.isClosed();
    }

    void reset(const juce::String& title)
    {
        window.reset();
        window.setName(title);
    }

    void repaint()
    {
        window.repaint();
    }

    Point<int> getScreenPosition()
    {
        if (window.isClosed())
            return window.getLastPos();
        else
            return window.getScreenPosition();
    }

    void setScreenPos (int x, int y)
    {
        if (! window.isClosed())
            window.setTopLeftPosition(x, y);
    }

    //==============================================================================
    static void doRun (LV2_External_UI_Widget* _this_)
    {
        const juce::MessageManagerLock mmLock;
        JuceLv2ExternalUIWrapper* self = (JuceLv2ExternalUIWrapper*) _this_;

        if (! self->isClosed())
            self->window.repaint();
    }

    static void doShow (LV2_External_UI_Widget* _this_)
    {
        const juce::MessageManagerLock mmLock;
        JuceLv2ExternalUIWrapper* self = (JuceLv2ExternalUIWrapper*) _this_;

        if (! self->isClosed())
        {
            if (! self->window.isOnDesktop())
                self->window.addToDesktop();

            self->window.restoreLastPos();
            self->window.setVisible(true);
        }
    }

    static void doHide (LV2_External_UI_Widget* _this_)
    {
        const juce::MessageManagerLock mmLock;
        JuceLv2ExternalUIWrapper* self = (JuceLv2ExternalUIWrapper*) _this_;

        if (! self->isClosed())
        {
            self->window.saveLastPos();
            self->window.setVisible(false);
        }
    }

private:
    JuceLv2ExternalUIWindow window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceLv2ExternalUIWrapper);
};

//==============================================================================
/**
    Juce LV2 Parent UI container, listens for resize events and passes them to ui-resize
*/
class JuceLv2ParentContainer : public Component
{
public:
    JuceLv2ParentContainer (juce::AudioProcessorEditor* editor, const LV2UI_Resize* uiResize_)
        : uiResize(uiResize_)
    {
        setOpaque (true);
        editor->setOpaque (true);
        setBounds (editor->getBounds());

        editor->setTopLeftPosition (0, 0);
        addAndMakeVisible (editor);
    }

    ~JuceLv2ParentContainer()
    {
    }

    void paint (juce::Graphics&) {}
    void paintOverChildren (juce::Graphics&) {}

    void childBoundsChanged (Component* child)
    {
        const int cw = child->getWidth();
        const int ch = child->getHeight();

#if JUCE_LINUX
        XResizeWindow (display.display, (Window) getWindowHandle(), cw, ch);
#else
        setSize (cw, ch);
#endif

        if (uiResize != nullptr)
            uiResize->ui_resize (uiResize->handle, cw, ch);
    }

    void reset (const LV2UI_Resize* uiResize_)
    {
        uiResize = uiResize_;

        if (uiResize != nullptr)
            uiResize->ui_resize (uiResize->handle, getWidth(), getHeight());
    }

private:
    //==============================================================================
    const LV2UI_Resize* uiResize;
#if JUCE_LINUX
    ScopedXDisplay display;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceLv2ParentContainer);
};

//==============================================================================
/**
    Juce LV2 UI handle
*/
class JuceLv2UIWrapper : public juce::AudioProcessorListener,
                         public juce::Timer
{
public:
    JuceLv2UIWrapper (juce::AudioProcessor* filter_, LV2UI_Write_Function writeFunction_, LV2UI_Controller controller_,
                      LV2UI_Widget* widget, const LV2_Feature* const* features, bool isExternal_)
        : filter (filter_),
          writeFunction (writeFunction_),
          controller (controller_),
          isExternal (isExternal_),
          controlPortOffset (0),
          lastProgramCount (0),
          uiTouch (nullptr),
          programsHost (nullptr),
          externalUIHost (nullptr),
          lastExternalUIPos (-1, -1),
          uiResize (nullptr)
    {
        jassert (filter != nullptr);

        filter->addListener(this);

        if (filter->hasEditor())
        {
            editor = filter->createEditorIfNeeded();

            if (editor == nullptr)
            {
                *widget = nullptr;
                return;
            }
        }

        for (int i = 0; features[i] != nullptr; ++i)
        {
            if (strcmp(features[i]->URI, LV2_UI__touch) == 0)
                uiTouch = (const LV2UI_Touch*)features[i]->data;

            else if (strcmp(features[i]->URI, LV2_PROGRAMS__Host) == 0)
                programsHost = (const LV2_Programs_Host*)features[i]->data;
        }

        if (isExternal)
        {
            resetExternalUI (features);

            if (externalUIHost != nullptr)
            {
                juce::String title (filter->getName());

                if (externalUIHost->plugin_human_id != nullptr)
                    title = externalUIHost->plugin_human_id;

                externalUI = new JuceLv2ExternalUIWrapper (editor, title);
                *widget = externalUI;
                startTimer (100);
            }
            else
            {
                *widget = nullptr;
            }
        }
        else
        {
            resetParentUI (features);

            if (parentContainer != nullptr)
               *widget = parentContainer->getWindowHandle();
            else
               *widget = nullptr;
        }

        controlPortOffset += 1; // MIDI output
        controlPortOffset += 1; // time Position and MIDI input ||
        controlPortOffset += 1; // freewheel
        controlPortOffset += 1; // latency
        controlPortOffset += filter->getTotalNumInputChannels();
        controlPortOffset += filter->getTotalNumOutputChannels();

        lastProgramCount = filter->getNumPrograms();
    }

    ~JuceLv2UIWrapper()
    {
        juce::PopupMenu::dismissAllActiveMenus();

        filter->removeListener(this);

        parentContainer = nullptr;
        externalUI = nullptr;
        externalUIHost = nullptr;

        if (editor != nullptr)
        {
            filter->editorBeingDeleted (editor);
            editor = nullptr;
        }
    }

    //==============================================================================
    // LV2 core calls

    void lv2Cleanup()
    {
        const juce::MessageManagerLock mmLock;

        if (isExternal)
        {
            if (isTimerRunning())
                stopTimer();

            externalUIHost = nullptr;

            if (externalUI != nullptr)
            {
                lastExternalUIPos = externalUI->getScreenPosition();
                externalUI->close();
            }
        }
        else
        {
            if (parentContainer)
            {
                parentContainer->setVisible (false);
                if (parentContainer->isOnDesktop())
                    parentContainer->removeFromDesktop();
            }
        }
    }

    //==============================================================================
    // Juce calls

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float newValue)
    {
        if (writeFunction != nullptr && controller != nullptr)
            writeFunction (controller, index + controlPortOffset, sizeof (float), 0, &newValue);
    }

    void audioProcessorChanged (juce::AudioProcessor*)
    {
        if (filter != nullptr && programsHost != nullptr)
        {
            if (filter->getNumPrograms() != lastProgramCount)
            {
                programsHost->program_changed (programsHost->handle, -1);
                lastProgramCount = filter->getNumPrograms();
            }
            else
                programsHost->program_changed (programsHost->handle, filter->getCurrentProgram());
        }
    }

    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex)
    {
        if (uiTouch != nullptr)
            uiTouch->touch (uiTouch->handle, parameterIndex + controlPortOffset, true);
    }

    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int parameterIndex)
    {
        if (uiTouch != nullptr)
            uiTouch->touch (uiTouch->handle, parameterIndex + controlPortOffset, false);
    }

    void timerCallback()
    {
        if (externalUI != nullptr && externalUI->isClosed())
        {
            if (externalUIHost != nullptr)
                externalUIHost->ui_closed (controller);

            if (isTimerRunning())
                stopTimer();
        }
    }

    //==============================================================================
    void resetIfNeeded (LV2UI_Write_Function writeFunction_, LV2UI_Controller controller_, LV2UI_Widget* widget,
                        const LV2_Feature* const* features)
    {
        writeFunction = writeFunction_;
        controller = controller_;
        uiTouch = nullptr;
        programsHost = nullptr;

        for (int i = 0; features[i] != nullptr; ++i)
        {
            if (strcmp(features[i]->URI, LV2_UI__touch) == 0)
                uiTouch = (const LV2UI_Touch*)features[i]->data;

            else if (strcmp(features[i]->URI, LV2_PROGRAMS__Host) == 0)
                programsHost = (const LV2_Programs_Host*)features[i]->data;
        }

        if (isExternal)
        {
            resetExternalUI (features);
            *widget = externalUI;
        }
        else
        {
            resetParentUI (features);
            *widget = parentContainer->getWindowHandle();
        }
    }

    void repaint()
    {
        const juce::MessageManagerLock mmLock;

        if (editor != nullptr)
            editor->repaint();

        if (parentContainer != nullptr)
            parentContainer->repaint();

        if (externalUI != nullptr)
            externalUI->repaint();
    }

private:
    juce::AudioProcessor* const filter;
    juce::ScopedPointer<juce::AudioProcessorEditor> editor;

    LV2UI_Write_Function writeFunction;
    LV2UI_Controller controller;
    const bool isExternal;

    uint32_t controlPortOffset;
    int lastProgramCount;

    const LV2UI_Touch* uiTouch;
    const LV2_Programs_Host* programsHost;

    juce::ScopedPointer<JuceLv2ExternalUIWrapper> externalUI;
    const LV2_External_UI_Host* externalUIHost;
    Point<int> lastExternalUIPos;

    juce::ScopedPointer<JuceLv2ParentContainer> parentContainer;
    const LV2UI_Resize* uiResize;

#if JUCE_LINUX
    ScopedXDisplay display;
#endif

    //==============================================================================
    void resetExternalUI (const LV2_Feature* const* features)
    {
        externalUIHost = nullptr;

        for (int i = 0; features[i] != nullptr; ++i)
        {
            if (strcmp(features[i]->URI, LV2_EXTERNAL_UI__Host) == 0)
            {
                externalUIHost = (const LV2_External_UI_Host*)features[i]->data;
                break;
            }
        }

        if (externalUI != nullptr)
        {
            juce::String title(filter->getName());

            if (externalUIHost->plugin_human_id != nullptr)
                title = externalUIHost->plugin_human_id;

            if (lastExternalUIPos.getX() != -1 && lastExternalUIPos.getY() != -1)
                externalUI->setScreenPos(lastExternalUIPos.getX(), lastExternalUIPos.getY());

            externalUI->reset(title);
            startTimer (100);
        }
    }

    void resetParentUI (const LV2_Feature* const* features)
    {
        void* parent = nullptr;
        uiResize = nullptr;

        for (int i = 0; features[i] != nullptr; ++i)
        {
            if (strcmp(features[i]->URI, LV2_UI__parent) == 0)
                parent = features[i]->data;

            else if (strcmp(features[i]->URI, LV2_UI__resize) == 0)
                uiResize = (const LV2UI_Resize*)features[i]->data;
        }

        if (parent != nullptr)
        {
            if (parentContainer == nullptr)
                parentContainer = new JuceLv2ParentContainer (editor, uiResize);

            parentContainer->setVisible (false);

            if (parentContainer->isOnDesktop())
                parentContainer->removeFromDesktop();

            parentContainer->addToDesktop (0, parent);

#if JUCE_LINUX
            Window hostWindow = (Window) parent;
            Window editorWnd  = (Window) parentContainer->getWindowHandle();
            XReparentWindow (display.display, editorWnd, hostWindow, 0, 0);
#endif

            parentContainer->reset (uiResize);
            parentContainer->setVisible (true);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceLv2UIWrapper)
};

//==============================================================================
/**
    Juce LV2 handle
*/

static juce::AudioProcessor* JuceLv2CreateFilter()
{
    const juce::MessageManagerLock mmLock;
    return createPluginFilterOfType (juce::AudioProcessor::wrapperType_Undefined);
}

class JuceLv2Wrapper : public juce::AudioPlayHead
{
public:
    //==============================================================================
    JuceLv2Wrapper (double sampleRate_, const LV2_Feature* const* features) :
        filter(JuceLv2CreateFilter()),
        bufferSize (2048),
        sampleRate (sampleRate_),
        uridMap (nullptr),
        uridAtomBlank (0),
        uridAtomObject (0),
        uridAtomDouble (0),
        uridAtomFloat (0),
        uridAtomInt (0),
        uridAtomLong (0),
        uridAtomSequence (0),
        uridMidiEvent (0),
        uridTimePos (0),
        uridTimeBar (0),
        uridTimeBarBeat (0),
        uridTimeBeatsPerBar (0),
        uridTimeBeatsPerMinute (0),
        uridTimeBeatUnit (0),
        uridTimeFrame (0),
        uridTimeSpeed (0),
        usingNominalBlockLength (false)
    {
        jassert (filter != nullptr);
        filter->setPlayConfigDetails (filter->getTotalNumInputChannels(), filter->getTotalNumOutputChannels(), 0, 0);
        filter->setPlayHead (this);
        portEventsIn = nullptr;
        portMidiOut = nullptr;
        portFreewheel = nullptr;
        portLatency = nullptr;
        portAudioIns.insertMultiple(0, nullptr, filter->getTotalNumInputChannels());
        portAudioOuts.insertMultiple(0, nullptr, filter->getTotalNumOutputChannels());
        portControls.insertMultiple(0, nullptr, filter->getNumParameters());

        for (int i=0; i < filter->getNumParameters(); ++i)
            lastControlValues.add (filter->getParameter(i));

        curPosInfo.resetToDefault();

        // we need URID_Map first
        for (int i = 0; features[i] != nullptr; ++i)
        {
            if (strcmp(features[i]->URI, LV2_URID__map) == 0)
            {
                uridMap = (const LV2_URID_Map*)features[i]->data;
                break;
            }
        }

        // we require uridMap to work properly (it's set as required feature)
        jassert (uridMap != nullptr);

        if (uridMap != nullptr)
        {
            uridAtomBlank = uridMap->map(uridMap->handle, LV2_ATOM__Blank);
            uridAtomObject = uridMap->map(uridMap->handle, LV2_ATOM__Object);
            uridAtomDouble = uridMap->map(uridMap->handle, LV2_ATOM__Double);
            uridAtomFloat = uridMap->map(uridMap->handle, LV2_ATOM__Float);
            uridAtomInt = uridMap->map(uridMap->handle, LV2_ATOM__Int);
            uridAtomLong = uridMap->map(uridMap->handle, LV2_ATOM__Long);
            uridAtomSequence = uridMap->map(uridMap->handle, LV2_ATOM__Sequence);
            uridMidiEvent = uridMap->map(uridMap->handle, LV2_MIDI__MidiEvent);
            uridTimePos = uridMap->map(uridMap->handle, LV2_TIME__Position);
            uridTimeBar = uridMap->map(uridMap->handle, LV2_TIME__bar);
            uridTimeBarBeat = uridMap->map(uridMap->handle, LV2_TIME__barBeat);
            uridTimeBeatsPerBar = uridMap->map(uridMap->handle, LV2_TIME__beatsPerBar);
            uridTimeBeatsPerMinute = uridMap->map(uridMap->handle, LV2_TIME__beatsPerMinute);
            uridTimeBeatUnit = uridMap->map(uridMap->handle, LV2_TIME__beatUnit);
            uridTimeFrame = uridMap->map(uridMap->handle, LV2_TIME__frame);
            uridTimeSpeed = uridMap->map(uridMap->handle, LV2_TIME__speed);

            for (int i = 0; features[i] != nullptr; ++i)
            {
                if (strcmp(features[i]->URI, LV2_OPTIONS__options) == 0)
                {
                    const LV2_Options_Option* options = (const LV2_Options_Option*)features[i]->data;

                    for (int j=0; options[j].key != 0; ++j)
                    {
                        if (options[j].key == uridMap->map(uridMap->handle, LV2_BUF_SIZE__nominalBlockLength))
                        {
                            if (options[j].type == uridAtomInt)
                            {
                                bufferSize = *(int*)options[j].value;
                                usingNominalBlockLength = true;
                            }
                            break;
                        }

                        if (options[j].key == uridMap->map(uridMap->handle, LV2_BUF_SIZE__maxBlockLength))
                        {
                            if (options[j].type == uridAtomInt)
                            {
                                bufferSize = *(int*)options[j].value;
                            }
                            // no break, continue in case host supports nominalBlockLength
                        }
                    }
                    break;
                }
            }
        }

        progDesc.bank = 0;
        progDesc.program = 0;
        progDesc.name = nullptr;
    }

    ~JuceLv2Wrapper ()
    {
        if (progDesc.name != nullptr)
        {
            free((void*)progDesc.name);
        }
    }

    //==============================================================================
    // LV2 core calls

    void lv2ConnectPort (uint32_t portId, void* dataLocation)
    {
        uint32_t index = 0;
        if (portId == 0)
        {
            portEventsIn = (LV2_Atom_Sequence*)dataLocation;
        }

        if (portId == index++)
        {
            portMidiOut = (LV2_Atom_Sequence*)dataLocation;
            return;
        }

        if (portId == index++)
        {
            portFreewheel = (float*)dataLocation;
            return;
        }

        if (portId == index++)
        {
            portLatency = (float*)dataLocation;
            return;
        }

        for (int i = 0; i < filter->getTotalNumInputChannels(); ++i)
        {
            if (portId == index++)
            {
                portAudioIns.getReference(i) = static_cast<float*>(dataLocation);
                return;
            }
        }

        for (int i = 0; i < filter->getTotalNumOutputChannels(); ++i)
        {
            if (portId == index++)
            {
                portAudioOuts.getReference(i) = static_cast<float*>(dataLocation);
                return;
            }
        }
    
        for (int i = 0; i < filter->getNumParameters(); ++i)
        {
            if (portId == index++)
            {
                portControls.set(i, static_cast<float*>(dataLocation));
                return;
            }
        }
    }

    void lv2Activate()
    {
        filter->prepareToPlay (sampleRate, bufferSize);
        filter->setPlayConfigDetails (filter->getTotalNumInputChannels(), filter->getTotalNumOutputChannels(), sampleRate, bufferSize);
        channels.calloc (filter->getTotalNumInputChannels() + filter->getTotalNumOutputChannels());
        midiEvents.ensureSize (2048);
        midiEvents.clear();
    }

    void lv2Deactivate()
    {
        filter->releaseResources();
        channels.free();
    }

    void lv2Run (uint32_t sampleCount)
    {
        if (portLatency != nullptr)
        {
            *portLatency = filter->getLatencySamples();
        }

        if (portFreewheel != nullptr)
        {
            filter->setNonRealtime (*portFreewheel >= 0.5f);
        }

        if (sampleCount == 0)
        {
            /**
               LV2 pre-roll
               Hosts might use this to force plugins to update its output control ports.
               (plugins can only access port locations during run) */
            return;
        }

        // Check for updated parameters
        {
            float curValue;

            for (int i = 0; i < portControls.size(); ++i)
            {
                if (portControls[i] != nullptr)
                {
                    curValue = *portControls[i];

                    if (lastControlValues[i] != curValue)
                    {
                        filter->setParameter (i, curValue);
                        lastControlValues.setUnchecked (i, curValue);
                    }
                }
            }
        }

        {
            const juce::ScopedLock sl (filter->getCallbackLock());

            if (filter->isSuspended() && false)
            {
                for (int i = 0; i < filter->getTotalNumOutputChannels(); ++i)
                    juce::zeromem (portAudioOuts[i], sizeof (float) * sampleCount);
            }
            else
            {
                int i;
                for (i = 0; i < filter->getTotalNumOutputChannels(); ++i)
                {
                    channels[i] = portAudioOuts[i];

                    if (i < filter->getTotalNumInputChannels() && portAudioIns[i] != portAudioOuts[i])
                        juce::FloatVectorOperations::copy (portAudioOuts [i], portAudioIns[i], sampleCount);
                }

                for (; i < filter->getTotalNumInputChannels(); ++i)
                    channels [i] = portAudioIns[i];

                if (portEventsIn != nullptr)
                {
                    midiEvents.clear();

                    LV2_ATOM_SEQUENCE_FOREACH(portEventsIn, iter)
                    {
                        const LV2_Atom_Event* event = (const LV2_Atom_Event*)iter;

                        if (event == nullptr)
                            continue;
                        if (event->time.frames >= sampleCount)
                            break;
                        if (event->body.type == uridMidiEvent)
                        {
                            const uint8_t* data = (const uint8_t*)(event + 1);
                            midiEvents.addEvent(data, event->body.size, static_cast<int>(event->time.frames));
                            continue;
                        }
                        if (event->body.type == uridAtomBlank || event->body.type == uridAtomObject)
                        {
                            const LV2_Atom_Object* obj = (LV2_Atom_Object*)&event->body;

                            if (obj->body.otype != uridTimePos)
                                continue;

                            LV2_Atom* bar = nullptr;
                            LV2_Atom* barBeat = nullptr;
                            LV2_Atom* beatUnit = nullptr;
                            LV2_Atom* beatsPerBar = nullptr;
                            LV2_Atom* beatsPerMinute = nullptr;
                            LV2_Atom* frame = nullptr;
                            LV2_Atom* speed = nullptr;

                            lv2_atom_object_get (obj,
                                                 uridTimeBar, &bar,
                                                 uridTimeBarBeat, &barBeat,
                                                 uridTimeBeatUnit, &beatUnit,
                                                 uridTimeBeatsPerBar, &beatsPerBar,
                                                 uridTimeBeatsPerMinute, &beatsPerMinute,
                                                 uridTimeFrame, &frame,
                                                 uridTimeSpeed, &speed,
                                                 nullptr);

                            // need to handle this first as other values depend on it
                            if (speed != nullptr)
                            {
                                if (speed->type == uridAtomDouble)
                                    lastPositionData.speed = ((LV2_Atom_Double*)speed)->body;
                                else if (speed->type == uridAtomFloat)
                                    lastPositionData.speed = ((LV2_Atom_Float*)speed)->body;
                                else if (speed->type == uridAtomInt)
                                    lastPositionData.speed = ((LV2_Atom_Int*)speed)->body;
                                else if (speed->type == uridAtomLong)
                                    lastPositionData.speed = ((LV2_Atom_Long*)speed)->body;

                                curPosInfo.isPlaying = lastPositionData.speed != 0.0;
                            }

                            if (bar != nullptr)
                            {
                                if (bar->type == uridAtomDouble)
                                    lastPositionData.bar = ((LV2_Atom_Double*)bar)->body;
                                else if (bar->type == uridAtomFloat)
                                    lastPositionData.bar = ((LV2_Atom_Float*)bar)->body;
                                else if (bar->type == uridAtomInt)
                                    lastPositionData.bar = ((LV2_Atom_Int*)bar)->body;
                                else if (bar->type == uridAtomLong)
                                    lastPositionData.bar = ((LV2_Atom_Long*)bar)->body;
                            }

                            if (barBeat != nullptr)
                            {
                                if (barBeat->type == uridAtomDouble)
                                    lastPositionData.barBeat = ((LV2_Atom_Double*)barBeat)->body;
                                else if (barBeat->type == uridAtomFloat)
                                    lastPositionData.barBeat = ((LV2_Atom_Float*)barBeat)->body;
                                else if (barBeat->type == uridAtomInt)
                                    lastPositionData.barBeat = ((LV2_Atom_Int*)barBeat)->body;
                                else if (barBeat->type == uridAtomLong)
                                    lastPositionData.barBeat = ((LV2_Atom_Long*)barBeat)->body;
                            }

                            if (beatUnit != nullptr)
                            {
                                if (beatUnit->type == uridAtomDouble)
                                    lastPositionData.beatUnit = ((LV2_Atom_Double*)beatUnit)->body;
                                else if (beatUnit->type == uridAtomFloat)
                                    lastPositionData.beatUnit = ((LV2_Atom_Float*)beatUnit)->body;
                                else if (beatUnit->type == uridAtomInt)
                                    lastPositionData.beatUnit = ((LV2_Atom_Int*)beatUnit)->body;
                                else if (beatUnit->type == uridAtomLong)
                                    lastPositionData.beatUnit = static_cast<uint32_t>(((LV2_Atom_Long*)beatUnit)->body);

                                if (lastPositionData.beatUnit > 0)
                                    curPosInfo.timeSigDenominator = lastPositionData.beatUnit;
                            }

                            if (beatsPerBar != nullptr)
                            {
                                if (beatsPerBar->type == uridAtomDouble)
                                    lastPositionData.beatsPerBar = ((LV2_Atom_Double*)beatsPerBar)->body;
                                else if (beatsPerBar->type == uridAtomFloat)
                                    lastPositionData.beatsPerBar = ((LV2_Atom_Float*)beatsPerBar)->body;
                                else if (beatsPerBar->type == uridAtomInt)
                                    lastPositionData.beatsPerBar = ((LV2_Atom_Int*)beatsPerBar)->body;
                                else if (beatsPerBar->type == uridAtomLong)
                                    lastPositionData.beatsPerBar = ((LV2_Atom_Long*)beatsPerBar)->body;

                                if (lastPositionData.beatsPerBar > 0.0f)
                                    curPosInfo.timeSigNumerator = lastPositionData.beatsPerBar;
                            }

                            if (beatsPerMinute != nullptr)
                            {
                                if (beatsPerMinute->type == uridAtomDouble)
                                    lastPositionData.beatsPerMinute = ((LV2_Atom_Double*)beatsPerMinute)->body;
                                else if (beatsPerMinute->type == uridAtomFloat)
                                    lastPositionData.beatsPerMinute = ((LV2_Atom_Float*)beatsPerMinute)->body;
                                else if (beatsPerMinute->type == uridAtomInt)
                                    lastPositionData.beatsPerMinute = ((LV2_Atom_Int*)beatsPerMinute)->body;
                                else if (beatsPerMinute->type == uridAtomLong)
                                    lastPositionData.beatsPerMinute = ((LV2_Atom_Long*)beatsPerMinute)->body;

                                if (lastPositionData.beatsPerMinute > 0.0f)
                                {
                                    curPosInfo.bpm = lastPositionData.beatsPerMinute;

                                    if (lastPositionData.speed != 0)
                                        curPosInfo.bpm *= std::abs(lastPositionData.speed);
                                }
                            }

                            if (frame != nullptr)
                            {
                                if (frame->type == uridAtomDouble)
                                    lastPositionData.frame = ((LV2_Atom_Double*)frame)->body;
                                else if (frame->type == uridAtomFloat)
                                    lastPositionData.frame = ((LV2_Atom_Float*)frame)->body;
                                else if (frame->type == uridAtomInt)
                                    lastPositionData.frame = ((LV2_Atom_Int*)frame)->body;
                                else if (frame->type == uridAtomLong)
                                    lastPositionData.frame = ((LV2_Atom_Long*)frame)->body;

                                if (lastPositionData.frame >= 0)
                                {
                                    curPosInfo.timeInSamples = lastPositionData.frame;
                                    curPosInfo.timeInSeconds = double(curPosInfo.timeInSamples)/sampleRate;
                                }
                            }

                            if (lastPositionData.bar >= 0 && lastPositionData.beatsPerBar > 0.0f)
                            {
                                curPosInfo.ppqPositionOfLastBarStart = lastPositionData.bar * lastPositionData.beatsPerBar;

                                if (lastPositionData.barBeat >= 0.0f)
                                    curPosInfo.ppqPosition = curPosInfo.ppqPositionOfLastBarStart + lastPositionData.barBeat;
                            }

                            lastPositionData.extraValid = (lastPositionData.beatsPerMinute > 0.0 &&
                                                           lastPositionData.beatUnit > 0 &&
                                                           lastPositionData.beatsPerBar > 0.0f);
                        }
                    }
                }
                {
                    juce::AudioSampleBuffer chans (channels, juce::jmax (filter->getTotalNumInputChannels(), filter->getTotalNumOutputChannels()), sampleCount);
                    filter->processBlock (chans, midiEvents);
                }
            }
        }

        // update timePos for next callback
        if (lastPositionData.speed != 0.0)
        {
            if (lastPositionData.speed > 0.0)
            {
                // playing forwards
                lastPositionData.frame += sampleCount;
            }
            else
            {
                // playing backwards
                lastPositionData.frame -= sampleCount;

                if (lastPositionData.frame < 0)
                    lastPositionData.frame = 0;
            }

            curPosInfo.timeInSamples = lastPositionData.frame;
            curPosInfo.timeInSeconds = double(curPosInfo.timeInSamples)/sampleRate;

            if (lastPositionData.extraValid)
            {
                const double beatsPerMinute = lastPositionData.beatsPerMinute * lastPositionData.speed;
                const double framesPerBeat  = 60.0 * sampleRate / beatsPerMinute;
                const double addedBarBeats  = double(sampleCount) / framesPerBeat;

                if (lastPositionData.bar >= 0 && lastPositionData.barBeat >= 0.0f)
                {
                    lastPositionData.bar    += std::floor((lastPositionData.barBeat+addedBarBeats)/
                                                           lastPositionData.beatsPerBar);
                    lastPositionData.barBeat = std::fmod(lastPositionData.barBeat+addedBarBeats,
                                                         lastPositionData.beatsPerBar);

                    if (lastPositionData.bar < 0)
                        lastPositionData.bar = 0;

                    curPosInfo.ppqPositionOfLastBarStart = lastPositionData.bar * lastPositionData.beatsPerBar;
                    curPosInfo.ppqPosition = curPosInfo.ppqPositionOfLastBarStart + lastPositionData.barBeat;
                }

                curPosInfo.bpm = std::abs(beatsPerMinute);
            }
        }

        if (portMidiOut != nullptr)
        {
            const uint32_t capacity = portMidiOut->atom.size;

            portMidiOut->atom.size = sizeof(LV2_Atom_Sequence_Body);
            portMidiOut->atom.type = uridAtomSequence;
            portMidiOut->body.unit = 0;
            portMidiOut->body.pad  = 0;

            if (! midiEvents.isEmpty())
            {
                const uint8_t* midiEventData;
                int midiEventSize, midiEventPosition;
                juce::MidiBuffer::Iterator i (midiEvents);

                uint32_t size, offset = 0;
                LV2_Atom_Event* aev;

                while (i.getNextEvent (midiEventData, midiEventSize, midiEventPosition))
                {
                    jassert (midiEventPosition >= 0 && midiEventPosition < (int)sampleCount);

                    if (sizeof(LV2_Atom_Event) + midiEventSize > capacity - offset)
                        break;

                    aev = (LV2_Atom_Event*)((char*)LV2_ATOM_CONTENTS(LV2_Atom_Sequence, portMidiOut) + offset);
                    aev->time.frames = midiEventPosition;
                    aev->body.type   = uridMidiEvent;
                    aev->body.size   = midiEventSize;
                    memcpy(LV2_ATOM_BODY(&aev->body), midiEventData, midiEventSize);

                    size    = lv2_atom_pad_size(sizeof(LV2_Atom_Event) + midiEventSize);
                    offset += size;
                    portMidiOut->atom.size += size;
                }

                midiEvents.clear();
            }
        }
        else if (! midiEvents.isEmpty())
        {
            midiEvents.clear();
        }
    }

    //==============================================================================
    // LV2 extended calls

    uint32_t lv2GetOptions (LV2_Options_Option* options)
    {
        // currently unused
        juce::ignoreUnused(options);

        return LV2_OPTIONS_SUCCESS;
    }

    uint32_t lv2SetOptions (const LV2_Options_Option* options)
    {
        for (int j=0; options[j].key != 0; ++j)
        {
            if (options[j].key == uridMap->map(uridMap->handle, LV2_BUF_SIZE__nominalBlockLength))
            {
                if (options[j].type == uridAtomInt)
                    bufferSize = *(int*)options[j].value;
            }
            else if (options[j].key == uridMap->map(uridMap->handle, LV2_BUF_SIZE__maxBlockLength) && ! usingNominalBlockLength)
            {
                if (options[j].type == uridAtomInt)
                    bufferSize = *(int*)options[j].value;
            }
            else if (options[j].key == uridMap->map(uridMap->handle, LV2_CORE__sampleRate))
            {
                if (options[j].type == uridAtomDouble)
                    sampleRate = *(double*)options[j].value;
            }
        }

        return LV2_OPTIONS_SUCCESS;
    }

    const LV2_Program_Descriptor* lv2GetProgram (uint32_t index)
    {
        if (progDesc.name != nullptr)
        {
            free((void*)progDesc.name);
            progDesc.name = nullptr;
        }

        if ((int)index < filter->getNumPrograms())
        {
            progDesc.bank    = index / 128;
            progDesc.program = index % 128;
            progDesc.name    = strdup(filter->getProgramName(index).toUTF8());
            return &progDesc;
        }

        return nullptr;
    }

    void lv2SelectProgram (uint32_t bank, uint32_t program)
    {
        jassert (filter != nullptr);

        int realProgram = bank * 128 + program;

        if (realProgram < filter->getNumPrograms())
        {
            filter->setCurrentProgram(realProgram);

            // update input control ports now
            for (int i = 0; i < portControls.size(); ++i)
            {
                float value = filter->getParameter(i);

                if (portControls[i] != nullptr)
                    *portControls[i] = value;

                lastControlValues.set(i, value);
            }
        }
    }

    LV2_State_Status lv2SaveState (LV2_State_Store_Function store, LV2_State_Handle stateHandle)
    {
        jassert (filter != nullptr);

#if JucePlugin_WantsLV2StateString
        String stateData(filter->getStateInformationString().replace("\r\n","\n"));
        CharPointer_UTF8 charData(stateData.toUTF8());

        store (stateHandle,
               uridMap->map(uridMap->handle, JUCE_LV2_STATE_STRING_URI),
               charData.getAddress(),
               charData.sizeInBytes(),
               uridMap->map(uridMap->handle, LV2_ATOM__String),
               LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE);
#else
        juce::MemoryBlock chunkMemory;
        filter->getCurrentProgramStateInformation (chunkMemory);

        store (stateHandle,
               uridMap->map(uridMap->handle, JUCE_LV2_STATE_BINARY_URI),
               chunkMemory.getData(),
               chunkMemory.getSize(),
               uridMap->map(uridMap->handle, LV2_ATOM__Chunk),
               LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE);
#endif

        return LV2_STATE_SUCCESS;
    }

    LV2_State_Status lv2RestoreState (LV2_State_Retrieve_Function retrieve, LV2_State_Handle stateHandle, uint32_t flags)
    {
        jassert (filter != nullptr);

        size_t size = 0;
        uint32_t type = 0;
#if JucePlugin_WantsLV2StateString
        const void* data = retrieve (stateHandle, uridMap->map(uridMap->handle, JUCE_LV2_STATE_STRING_URI), &size, &type, &flags);
#else
        const void* data = retrieve (stateHandle, uridMap->map(uridMap->handle, JUCE_LV2_STATE_BINARY_URI), &size, &type, &flags);
#endif

        if (data == nullptr || size == 0 || type == 0)
            return LV2_STATE_ERR_UNKNOWN;

#if JucePlugin_WantsLV2StateString
        if (type == uridMap->map (uridMap->handle, LV2_ATOM__String))
        {
            String stateData (CharPointer_UTF8(static_cast<const char*>(data)));
            filter->setStateInformationString (stateData);
            if (ui != nullptr)
                ui->repaint();
            return LV2_STATE_SUCCESS;
        }
#else
        if (type == uridMap->map (uridMap->handle, LV2_ATOM__Chunk))
        {
            filter->setCurrentProgramStateInformation (data, static_cast<int>(size));
            if (ui != nullptr)
                ui->repaint();
            return LV2_STATE_SUCCESS;
        }
#endif

        return LV2_STATE_ERR_BAD_TYPE;
    }

    //==============================================================================
    // Juce calls

    bool getCurrentPosition (AudioPlayHead::CurrentPositionInfo& info)
    {
        info = curPosInfo;
        return true;
    }

    //==============================================================================
    JuceLv2UIWrapper* getUI (LV2UI_Write_Function writeFunction, LV2UI_Controller controller, LV2UI_Widget* widget,
                             const LV2_Feature* const* features, bool isExternal)
    {
        const juce::MessageManagerLock mmLock;

        if (ui != nullptr)
            ui->resetIfNeeded (writeFunction, controller, widget, features);
        else
            ui = new JuceLv2UIWrapper (filter, writeFunction, controller, widget, features, isExternal);

        return ui;
    }

private:
    juce::ScopedPointer<juce::AudioProcessor> filter;
    juce::ScopedPointer<JuceLv2UIWrapper> ui;
    juce::HeapBlock<float*> channels;
    juce::MidiBuffer midiEvents;

    LV2_Atom_Sequence* portEventsIn;
    LV2_Atom_Sequence* portMidiOut;
    float* portFreewheel;
    float* portLatency;
    juce::Array<float*> portAudioIns;
    juce::Array<float*> portAudioOuts;
    juce::Array<float*> portControls;

    uint32_t bufferSize;
    double sampleRate;
    juce::Array<float> lastControlValues;
    AudioPlayHead::CurrentPositionInfo curPosInfo;

    struct Lv2PositionData {
        int64_t  bar;
        float    barBeat;
        uint32_t beatUnit;
        float    beatsPerBar;
        float    beatsPerMinute;
        int64_t  frame;
        double   speed;
        bool     extraValid;

        Lv2PositionData()
            : bar(-1),
              barBeat(-1.0f),
              beatUnit(0),
              beatsPerBar(0.0f),
              beatsPerMinute(0.0f),
              frame(-1),
              speed(0.0),
              extraValid(false) {}
    };
    Lv2PositionData lastPositionData;

    const LV2_URID_Map* uridMap;
    LV2_URID uridAtomBlank;
    LV2_URID uridAtomObject;
    LV2_URID uridAtomDouble;
    LV2_URID uridAtomFloat;
    LV2_URID uridAtomInt;
    LV2_URID uridAtomLong;
    LV2_URID uridAtomSequence;
    LV2_URID uridMidiEvent;
    LV2_URID uridTimePos;
    LV2_URID uridTimeBar;
    LV2_URID uridTimeBarBeat;
    LV2_URID uridTimeBeatsPerBar;    // timeSigNumerator
    LV2_URID uridTimeBeatsPerMinute; // bpm
    LV2_URID uridTimeBeatUnit;       // timeSigDenominator
    LV2_URID uridTimeFrame;          // timeInSamples
    LV2_URID uridTimeSpeed;

    bool usingNominalBlockLength; // if false use maxBlockLength

    LV2_Program_Descriptor progDesc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceLv2Wrapper)
};

//==============================================================================
// LV2 descriptor functions

static LV2_Handle juceLV2_Instantiate (const LV2_Descriptor*, double sampleRate, const char*, const LV2_Feature* const* features)
{
    return new JuceLv2Wrapper (sampleRate, features);
}

static void juceLV2_ConnectPort (LV2_Handle handle, uint32_t port, void* dataLocation)
{
    //static_cast<JuceLv2Wrapper *>(handle)->lv2ConnectPort (port, dataLocation);
}

static void juceLV2_Activate (LV2_Handle handle)
{
    //static_cast<JuceLv2Wrapper *>(handle)->lv2Activate();
}

static void juceLV2_Run( LV2_Handle handle, uint32_t sampleCount)
{
    //static_cast<JuceLv2Wrapper *>(handle)->lv2Run (sampleCount);
}

static void juceLV2_Deactivate (LV2_Handle handle)
{
    //static_cast<JuceLv2Wrapper *>(handle)->lv2Deactivate();
}

static void juceLV2_Cleanup (LV2_Handle handle)
{
    delete static_cast<JuceLv2Wrapper *>(handle);
}

//==============================================================================
// LV2 extended functions

static uint32_t juceLV2_getOptions (LV2_Handle handle, LV2_Options_Option* options)
{
    return static_cast<JuceLv2Wrapper *>(handle)->lv2GetOptions(options);
}

static uint32_t juceLV2_setOptions (LV2_Handle handle, const LV2_Options_Option* options)
{
    return static_cast<JuceLv2Wrapper *>(handle)->lv2SetOptions(options);
}

static const LV2_Program_Descriptor* juceLV2_getProgram (LV2_Handle handle, uint32_t index)
{
    return static_cast<JuceLv2Wrapper *>(handle)->lv2GetProgram(index);
}

static void juceLV2_selectProgram (LV2_Handle handle, uint32_t bank, uint32_t program)
{
    static_cast<JuceLv2Wrapper *>(handle)->lv2SelectProgram(bank, program);
}

static LV2_State_Status juceLV2_SaveState (LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle stateHandle, uint32_t, const LV2_Feature* const*)
{
    return static_cast<JuceLv2Wrapper *>(handle)->lv2SaveState(store, stateHandle);
}

static LV2_State_Status juceLV2_RestoreState (LV2_Handle handle, LV2_State_Retrieve_Function retrieve, LV2_State_Handle stateHandle, uint32_t flags, const LV2_Feature* const*)
{
    return static_cast<JuceLv2Wrapper *>(handle)->lv2RestoreState(retrieve, stateHandle, flags);
}

static const void* juceLV2_ExtensionData (const char* uri)
{
    return nullptr;
    static const LV2_Options_Interface options = { juceLV2_getOptions, juceLV2_setOptions };
    static const LV2_Programs_Interface programs = { juceLV2_getProgram, juceLV2_selectProgram };
    static const LV2_State_Interface state = { juceLV2_SaveState, juceLV2_RestoreState };

    if (strcmp(uri, LV2_OPTIONS__interface) == 0)
        return &options;
    if (strcmp(uri, LV2_PROGRAMS__Interface) == 0)
        return &programs;
    if (strcmp(uri, LV2_STATE__interface) == 0)
        return &state;

    return nullptr;
}

//==============================================================================
// LV2 UI descriptor functions

static LV2UI_Handle juceLV2UI_Instantiate (LV2UI_Write_Function writeFunction, LV2UI_Controller controller,
                                           LV2UI_Widget* widget, const LV2_Feature* const* features, bool isExternal)
{
    for (int i = 0; features[i] != nullptr; ++i)
    {
        if (strcmp(features[i]->URI, LV2_INSTANCE_ACCESS_URI) == 0 && features[i]->data != nullptr)
        {
            JuceLv2Wrapper* wrapper = (JuceLv2Wrapper*)features[i]->data;
            return wrapper->getUI(writeFunction, controller, widget, features, isExternal);
        }
    }
    return nullptr;
}

static LV2UI_Handle juceLV2UI_InstantiateExternal (const LV2UI_Descriptor*, const char*, const char*, LV2UI_Write_Function writeFunction,
                                                   LV2UI_Controller controller, LV2UI_Widget* widget, const LV2_Feature* const* features)
{
    return juceLV2UI_Instantiate(writeFunction, controller, widget, features, true);
}

static LV2UI_Handle juceLV2UI_InstantiateParent (const LV2UI_Descriptor*, const char*, const char*, LV2UI_Write_Function writeFunction,
                                                 LV2UI_Controller controller, LV2UI_Widget* widget, const LV2_Feature* const* features)
{
    return juceLV2UI_Instantiate(writeFunction, controller, widget, features, false);
}

static void juceLV2UI_Cleanup (LV2UI_Handle handle)
{
    ((JuceLv2UIWrapper*)handle)->lv2Cleanup();
}

//==============================================================================
// static LV2 Descriptor objects

static const LV2_Descriptor JuceLv2Plugin = {
    strdup(JucePlugin_LV2URI),
    juceLV2_Instantiate,
    juceLV2_ConnectPort,
    juceLV2_Activate,
    juceLV2_Run,
    juceLV2_Deactivate,
    juceLV2_Cleanup,
    juceLV2_ExtensionData
};

static const LV2UI_Descriptor JuceLv2UI_External = {
    strdup((juce::String(JucePlugin_LV2URI) + juce::String("#ExternalUI")).toRawUTF8()),
    juceLV2UI_InstantiateExternal,
    juceLV2UI_Cleanup,
    nullptr,
    nullptr
};

static const LV2UI_Descriptor JuceLv2UI_Parent = {
    strdup((juce::String(JucePlugin_LV2URI) + juce::String("#ParentUI")).toRawUTF8()),
    juceLV2UI_InstantiateParent,
    juceLV2UI_Cleanup,
    nullptr,
    nullptr
};

static const struct DescriptorCleanup {
    DescriptorCleanup() {}
    ~DescriptorCleanup()
    {
        free((void*)JuceLv2Plugin.URI);
        free((void*)JuceLv2UI_External.URI);
        free((void*)JuceLv2UI_Parent.URI);
    }
} _descCleanup;

#if JUCE_WINDOWS
 #define JUCE_EXPORTED_FUNCTION extern "C" __declspec (dllexport)
#else
 #define JUCE_EXPORTED_FUNCTION extern "C" __attribute__ ((visibility("default")))
#endif

//==============================================================================
// startup code..

JUCE_EXPORTED_FUNCTION const LV2_Descriptor* lv2_descriptor (uint32_t index);
JUCE_EXPORTED_FUNCTION const LV2_Descriptor* lv2_descriptor (uint32_t index)
{
    return (index == 0) ? &JuceLv2Plugin : nullptr;
}

JUCE_EXPORTED_FUNCTION const LV2UI_Descriptor* lv2ui_descriptor (uint32_t index);
JUCE_EXPORTED_FUNCTION const LV2UI_Descriptor* lv2ui_descriptor (uint32_t index)
{
    switch (index)
    {
    case 0:
        return &JuceLv2UI_External;
    case 1:
        return &JuceLv2UI_Parent;
    default:
        return nullptr;
    }
}

#endif
