/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

#if (defined (__WINE__) && (JUCE_LINUX || JUCE_BSD)) || DOXYGEN

//==============================================================================
/**
    A winelib-specific class that creates a Wine HWND for hosting Windows
    plugin GUIs in-process, then reparents the HWND's X11 backing window
    into the JUCE host's X11 widget tree.

    Use this from a JUCE application built as a winelib ELF (the
    JUCE-as-Linux pivot via winelib_compat.h that keeps JUCE on its Linux
    paths but compiles into a winelib binary with Wine APIs reachable).
    The component creates a real HWND via CreateWindowExW so Windows-only
    VST3 / VST2 / CLAP plugins see kPlatformTypeHWND for their
    IPlugView::attached() / effEditOpen() handshake.  JUCE sees a normal
    Component holding an X11 child window; layout via setBounds() works
    the same as XEmbedComponent.

    Destruction is deferred: the X11 backing window is unmapped and
    reparented back to the root synchronously (to disentangle it from
    JUCE's widget tree), but the actual DestroyWindow is delayed ~1 s
    via Timer::callAfterDelay.  This mirrors yabridge's
    DeferredWin32Window pattern.  Under SCHED_FIFO@80 (NSPA RT) the
    plugin's PE-side IPlugView destructor chain runs without preemption
    from equal-prio threads, and queued X11 events the plugin generated
    can fire after its mirror has been torn down — yanking the host
    HWND synchronously races with that and produces Wine
    collided-unwind loops that exhaust the worker stack.

    @tags{GUI}
*/
class JUCE_API  WineHWNDEmbedComponent  : public Component
{
public:
    //==============================================================================
    /** Creates a winelib HWND embed wrapper.

        The HWND is created immediately via Wine's CreateWindowExW.  It is
        not reparented into JUCE's X11 tree until the component is added
        to a parent that has a peer; at that point the underlying X11
        backing window is reparented under the peer's native X11 window.
    */
    WineHWNDEmbedComponent (int initialWidth = 128, int initialHeight = 128);

    /** Destructor.  Triggers the deferred-destroy sequence described in
        the class overview. */
    ~WineHWNDEmbedComponent() override;

    //==============================================================================
    /** Returns the Wine HWND.

        Pass this to a Windows plugin's IPlugView::attached() with type
        kPlatformTypeHWND, or to a VST2 effEditOpen.

        Returned as void* so this header doesn't have to pull <windows.h>.
        Cast to HWND in code that has windows.h available.
    */
    void* getHWND() const noexcept;

    /** Forces the HWND (and its X11 backing window) to match this
        component's current on-screen bounds.  Equivalent to
        HWNDComponent::updateHWNDBounds / XEmbedComponent::updateEmbeddedBounds. */
    void updateEmbeddedBounds();

    /** Hides the embedded window and unmaps its X11 backing.  The HWND
        remains valid; the deferred destructor sequence still runs at
        destruction time.  Mirrors XEmbedComponent::removeClient. */
    void removeClient();

    //==============================================================================
    /** @internal */
    void paint (Graphics&) override;
    /** @internal */
    void resized() override;

private:
    class Pimpl;
    std::unique_ptr<Pimpl> pimpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WineHWNDEmbedComponent)
};

#endif

} // namespace juce
