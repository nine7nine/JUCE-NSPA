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

#if defined (__WINE__) && (JUCE_LINUX || JUCE_BSD)

// Native X11.  JUCE's prelude (winelib_compat.h) has already undef'd the
// _WIN32 family so Xlib compiles in its Linux flavour.  Xlib *calls* go
// through JUCE's X11Symbols dlsym indirection — same pattern as
// juce_XEmbedComponent_linux.cpp.
#include <X11/Xlib.h>

namespace juce
{

//==============================================================================
// Inline Win32 forward declarations — same pattern as the Phase 2
// LoadLibraryW patch in juce_VST3PluginFormatImpl.h.  Avoids pulling
// <windows.h>, which fights with JUCE's Linux mode in this TU.  On x86_64
// winelib `__stdcall` is `__attribute__((__ms_abi__))` — the ABI Wine's
// PE-side user32 / kernel32 use; winegcc maps it correctly.
extern "C"
{
    using WineHWND      = void*;
    using WineHMODULE   = void*;
    using WineHINSTANCE = void*;
    using WineHMENU     = void*;
    using WineHICON     = void*;
    using WineHCURSOR   = void*;
    using WineHBRUSH    = void*;
    using WineATOM      = unsigned short;
    using WineUINT      = unsigned int;
    using WineDWORD     = unsigned int;
    using WineBOOL      = int;
    using WineLONG      = int;
    using WineLRESULT   = intptr_t;
    using WineWPARAM    = uintptr_t;
    using WineLPARAM    = intptr_t;

    struct WinePOINT  { WineLONG x, y; };

    struct WineMSG
    {
        WineHWND  hwnd;
        WineUINT  message;
        WineWPARAM wParam;
        WineLPARAM lParam;
        WineDWORD time;
        WinePOINT pt;
        WineDWORD lPrivate;
    };

    using WineWNDPROC = WineLRESULT (__stdcall*) (WineHWND, WineUINT, WineWPARAM, WineLPARAM);

    struct WineWNDCLASSEXW
    {
        WineUINT        cbSize;
        WineUINT        style;
        WineWNDPROC     lpfnWndProc;
        int             cbClsExtra;
        int             cbWndExtra;
        WineHINSTANCE   hInstance;
        WineHICON       hIcon;
        WineHCURSOR     hCursor;
        WineHBRUSH      hbrBackground;
        const uint16_t* lpszMenuName;
        const uint16_t* lpszClassName;
        WineHICON       hIconSm;
    };

    constexpr WineDWORD WINE_WS_POPUP          = 0x80000000u;
    constexpr WineDWORD WINE_WS_EX_TOOLWINDOW  = 0x00000080u;
    constexpr WineUINT  WINE_CS_DBLCLKS        = 0x0008;
    constexpr WineUINT  WINE_PM_REMOVE         = 0x0001;
    constexpr int       WINE_SW_HIDE           = 0;
    constexpr int       WINE_SW_SHOWNORMAL     = 1;
    constexpr WineUINT  WINE_SWP_NOSIZE        = 0x0001;
    constexpr WineUINT  WINE_SWP_NOMOVE        = 0x0002;
    constexpr WineUINT  WINE_SWP_NOZORDER      = 0x0004;
    constexpr WineUINT  WINE_SWP_NOACTIVATE    = 0x0010;
    constexpr WineUINT  WINE_SWP_NOOWNERZORDER = 0x0200;
    constexpr WineUINT  WINE_SWP_DEFERERASE    = 0x2000;

    WineHMODULE __stdcall GetModuleHandleW (const uint16_t*);
    WineATOM    __stdcall RegisterClassExW (const WineWNDCLASSEXW*);
    WineHWND    __stdcall CreateWindowExW  (WineDWORD exStyle,
                                            const uint16_t* className,
                                            const uint16_t* windowName,
                                            WineDWORD style,
                                            int x, int y, int w, int h,
                                            WineHWND parent, WineHMENU menu,
                                            WineHINSTANCE instance, void* param);
    WineBOOL    __stdcall DestroyWindow    (WineHWND);
    WineBOOL    __stdcall ShowWindow       (WineHWND, int);
    WineBOOL    __stdcall SetWindowPos     (WineHWND, WineHWND zorderAfter,
                                            int x, int y, int w, int h,
                                            WineUINT flags);
    WineLRESULT __stdcall DefWindowProcW   (WineHWND, WineUINT, WineWPARAM, WineLPARAM);
    WineBOOL    __stdcall PeekMessageW     (WineMSG*, WineHWND,
                                            WineUINT min, WineUINT max,
                                            WineUINT remove);
    WineBOOL    __stdcall TranslateMessage (const WineMSG*);
    WineLRESULT __stdcall DispatchMessageW (const WineMSG*);
    void*       __stdcall GetPropA         (WineHWND, const char*);
} // extern "C"

//==============================================================================
namespace
{
    // Wine x11drv stores the X11 Window backing an HWND as a window
    // property.  Stable, public contract used by yabridge / LinVst / etc.
    constexpr const char* kWineX11WholeWindowProp = "__wine_x11_whole_window";

    // ~60 Hz pump: Win32 message queue + screen-position polling.  See
    // feedback memory entry `winelib-message-pump-required` for the Win32
    // pump rationale.
    constexpr int kPumpIntervalMs = 16;

    // Defer the actual DestroyWindow ~1 s past the C++ destructor so the
    // plugin's PE-side IPlugView destructor chain (which may queue X11
    // events targeting wine_x11_window) settles before teardown.  Mirrors
    // yabridge's DeferredWin32Window and the lifecycle fixes in
    // yabridge-nspa commits bb3a6f38 + e06f76de.
    constexpr int kDeferredDestroyMs = 1000;

    // UTF-16 literals — char16_t is guaranteed 16-bit (independent of
    // -fshort-wchar), which is what Wine PE-side WCHAR expects.  The
    // toolchain's -fno-short-wchar makes wchar_t 32-bit, so we must NOT
    // use L"..." here.
    constexpr const char16_t kWindowClassName[] = u"juce_winelib_plugin_embed";
    constexpr const char16_t kWindowName[]      = u"juce winelib plugin";

    WineLRESULT __stdcall windowProc (WineHWND hwnd,
                                      WineUINT msg,
                                      WineWPARAM w,
                                      WineLPARAM l)
    {
        return DefWindowProcW (hwnd, msg, w, l);
    }

    WineATOM ensureWindowClass()
    {
        static const WineATOM atom = []() -> WineATOM
        {
            WineWNDCLASSEXW wc {};
            wc.cbSize        = sizeof (wc);
            wc.style         = WINE_CS_DBLCLKS;
            wc.lpfnWndProc   = &windowProc;
            wc.hInstance     = GetModuleHandleW (nullptr);
            wc.lpszClassName = reinterpret_cast<const uint16_t*> (kWindowClassName);
            return RegisterClassExW (&wc);
        }();
        return atom;
    }

    Window lookupWineX11Window (WineHWND hwnd) noexcept
    {
        if (hwnd == nullptr)
            return 0;
        return (Window) (uintptr_t) GetPropA (hwnd, kWineX11WholeWindowProp);
    }
}

//==============================================================================
// Implementation strategy — NOT XEMBED, NOT a wrapper window
// ----------------------------------------------------------
// Earlier attempts that broke rendering:
//   (a) Intermediate "wrapper" X11 window with SubstructureRedirectMask
//       (yabridge 3.5+ pattern, intended to pin wine_x11_window inside
//       the wrapper).  Wine creates wine_x11_window with override_redirect
//       = TRUE for unmanaged WS_POPUPs (window.c:2480), so our XMapWindow
//       responses to MapRequest events bypass redirect — but the rendering
//       chain still didn't fire in our in-process JUCE setup.
//   (b) JUCE's XEmbedComponent host (full XEMBED protocol).  Wine's
//       XEMBED implementation has documented rendering issues with some
//       plugins (yabridge editor.h:136-149 explicitly notes Serum-class
//       breakage; we hit the same — black window).
//
// What rendered correctly:
//   (c) Direct XReparent of wine_x11_window straight into JUCE's peer X11
//       window.  Plugin drew fine.  Clicks were offset because Wine's
//       PE-side WND rect (data->rects.visible) wasn't updated to match
//       the new on-screen position.
//
// Why USER32 mouse routing cares about the WND rect:
//   X11DRV_ButtonPress (mouse.c:1533) creates an INPUT struct with
//   MOUSEEVENTF_ABSOLUTE + screen-relative root_x / root_y from the X11
//   event.  NtUserSendHardwareInput delivers to the input queue with
//   target = our HWND (via XFindContext on the X11 window).  When
//   USER32 dispatches the resulting WM_LBUTTONDOWN, it converts screen
//   coords to client coords by subtracting the target HWND's screen
//   position — read from the WND rect.  If the WND rect says (0,0,W,H)
//   but the window is actually at (peer + area, peer + area + W, ...)
//   the client coords are off by exactly (peer + area).  Exactly the
//   symptom we saw.
//
// The fix: take path (c) and ALSO update Wine's WND rect via a
// SetWindowPos with absolute screen coords.  Wine's pSetWindowPos will
// issue XConfigureWindow against wine_x11_window with the absolute coords
// — which is wrong at the X11 layer (those coords are interpreted as
// parent-relative once wine_x11_window is reparented under the peer).
// We immediately follow with our own XMoveResizeWindow(wine_x11_window,
// 0, 0, w, h) on our X11 connection to pin it back to the right place.
// The X server processes the two requests in order so wine_x11_window's
// visible position ends up where we want.
class WineHWNDEmbedComponent::Pimpl  : public ComponentMovementWatcher,
                                       private Timer
{
public:
    Pimpl (WineHWNDEmbedComponent& outer, int initialW, int initialH)
        : ComponentMovementWatcher (&outer),
          owner (outer)
    {
        display = X11Symbols::getInstance()->xOpenDisplay (nullptr);

        const WineATOM classAtom = ensureWindowClass();
        if (classAtom != 0)
        {
            const uint16_t* classRef = reinterpret_cast<const uint16_t*> (
                                            (uintptr_t) classAtom);

            // WS_POPUP gives us an HWND with no Win32 parent — Wine's
            // x11drv creates wine_x11_window immediately during win_data
            // allocation (window.c:2902-2908), so no ShowWindow is needed
            // before we can look it up.
            hwnd = CreateWindowExW (WINE_WS_EX_TOOLWINDOW,
                                    classRef,
                                    reinterpret_cast<const uint16_t*> (kWindowName),
                                    WINE_WS_POPUP,
                                    0, 0,
                                    jmax (1, initialW), jmax (1, initialH),
                                    nullptr, nullptr,
                                    GetModuleHandleW (nullptr),
                                    nullptr);
        }

        if (hwnd != nullptr)
            wineX11Window = lookupWineX11Window (hwnd);

        startTimer (kPumpIntervalMs);
    }

    ~Pimpl() override
    {
        stopTimer();

        // Disentangle wine_x11_window from JUCE's widget tree
        // SYNCHRONOUSLY so JUCE's Component destruction higher up (which
        // may tear down its own X11 peer) can't take our window with it.
        if (hwnd != nullptr)
            ShowWindow (hwnd, WINE_SW_HIDE);

        if (display != nullptr && wineX11Window != 0)
        {
            auto* x = X11Symbols::getInstance();
            x->xUnmapWindow (display, wineX11Window);
            x->xReparentWindow (display, wineX11Window,
                                DefaultRootWindow (display), 0, 0);
            x->xFlush (display);
        }

        // Defer the actual DestroyWindow ~1 s past this destructor so
        // the plugin's PE-side IPlugView destructor chain (which may
        // queue X11 events at wine_x11_window) settles before teardown.
        if (hwnd != nullptr)
        {
            const WineHWND deferredHwnd = hwnd;
            Timer::callAfterDelay (kDeferredDestroyMs,
                                   [deferredHwnd]() { DestroyWindow (deferredHwnd); });
        }

        if (display != nullptr)
            X11Symbols::getInstance()->xCloseDisplay (display);
    }

    WineHWND getHwnd() const noexcept    { return hwnd; }

    void updateBounds()
    {
        componentMovedOrResized (true, true);
    }

    void removeClient()
    {
        if (display != nullptr && wineX11Window != 0)
        {
            auto* x = X11Symbols::getInstance();
            x->xUnmapWindow (display, wineX11Window);
            x->xFlush (display);
        }
        if (hwnd != nullptr)
            ShowWindow (hwnd, WINE_SW_HIDE);
    }

    //==========================================================================
    // ComponentMovementWatcher overrides
    void componentMovedOrResized (bool /*wasMoved*/, bool /*wasResized*/) override
    {
        if (hwnd == nullptr || display == nullptr || wineX11Window == 0)
            return;

        auto* topLevel = owner.getTopLevelComponent();
        if (topLevel == nullptr)
            return;

        auto* peer = topLevel->getPeer();
        if (peer == nullptr)
            return;

        const auto area = peer->getAreaCoveredBy (owner);
        const int w = jmax (1, area.getWidth());
        const int h = jmax (1, area.getHeight());

        syncHwndScreenPosition (w, h);
    }

    using ComponentMovementWatcher::componentMovedOrResized;

    void componentPeerChanged() override
    {
        auto* peer = owner.getPeer();

        if (peer == currentPeer)
            return;

        currentPeer = peer;

        if (peer == nullptr || display == nullptr || hwnd == nullptr)
            return;

        if (wineX11Window == 0)
            wineX11Window = lookupWineX11Window (hwnd);

        // JUCE_LINUX peer->getNativeHandle() returns an X11 Window cast
        // to void* — same convention used by JUCE's LV2 host path
        // (juce_LV2PluginFormat.cpp:581).
        const Window parentX11 = (Window) (uintptr_t) peer->getNativeHandle();

        auto* x = X11Symbols::getInstance();

        if (parentX11 != 0 && wineX11Window != 0)
        {
            // Direct reparent — no intermediate wrapper.  This is the
            // path that previously rendered correctly.
            x->xReparentWindow (display, wineX11Window, parentX11, 0, 0);
            x->xSync (display, False);
        }

        // SW_SHOWNORMAL (not SW_SHOWNA): activates the window, which is
        // what Wine's is_window_managed() (window.c:439) uses to decide
        // managed-vs-unmanaged.  Line 449: "if (!(swp_flags &
        // (SWP_NOACTIVATE|SWP_HIDEWINDOW))) return TRUE;" — activating
        // the window WITHOUT SWP_NOACTIVATE makes Wine flag it as
        // managed.  Managed windows do NOT bail out of
        // window_update_client_config (window.c:1796) — meaning the
        // synthetic / explicit position updates we issue below actually
        // reach the WND rect that USER32's hit-testing reads.
        if (hwnd != nullptr)
            ShowWindow (hwnd, WINE_SW_SHOWNORMAL);

        // Second-chance: if wine_x11_window only came up after
        // ShowWindow, reparent now.
        if (wineX11Window == 0)
        {
            wineX11Window = lookupWineX11Window (hwnd);
            if (wineX11Window != 0 && parentX11 != 0)
            {
                x->xReparentWindow (display, wineX11Window, parentX11, 0, 0);
                x->xSync (display, False);
            }
        }

        componentMovedOrResized (true, true);
    }

    void componentVisibilityChanged() override
    {
        if (hwnd != nullptr)
            ShowWindow (hwnd, owner.isShowing() ? WINE_SW_SHOWNORMAL : WINE_SW_HIDE);
    }

    using ComponentMovementWatcher::componentVisibilityChanged;

    void componentBroughtToFront (Component& c) override
    {
        ComponentMovementWatcher::componentBroughtToFront (c);
    }

private:
    // Tell Wine where wine_x11_window actually IS on screen, then
    // immediately undo the X11-level damage that Wine's SetWindowPos
    // response causes.
    //
    // Step 1: SetWindowPos(hwnd, abs_x, abs_y, w, h)
    //   Updates Wine's PE-side WND rect (data->rects.window/visible) to
    //   the absolute screen coordinates.  This is what USER32 reads when
    //   it converts mouse-input screen coords to client coords before
    //   dispatching WM_LBUTTONDOWN / WM_MOUSEMOVE to the plugin.
    //
    //   Wine's pSetWindowPos chains through to x11drv, which calls
    //   XConfigureWindow on wine_x11_window with the absolute coords.
    //   Since wine_x11_window is reparented under peer's X11 window,
    //   the X server interprets those coords as parent-relative — which
    //   would place wine_x11_window FAR off-screen (peer_screen + abs).
    //
    // Step 2: XMoveResizeWindow(wineX11Window, 0, 0, w, h) on our display
    //   Issued back-to-back on a different X11 connection (our display vs.
    //   Wine's display).  The X server serializes both connections'
    //   requests; our move arrives shortly after Wine's, putting
    //   wine_x11_window back at the parent-relative origin (0, 0) — which
    //   is correct because peer's X11 window IS the desired on-screen
    //   origin for our embed.
    //
    //   The flicker window is microseconds at most.  No wrapper /
    //   SubstructureRedirect is needed because override_redirect on
    //   wine_x11_window (set by Wine for unmanaged popups) makes our
    //   move pass through immediately.
    void syncHwndScreenPosition (int w, int h) noexcept
    {
        if (hwnd == nullptr || display == nullptr || wineX11Window == 0)
            return;

        // Absolute screen position = wineX11Window's current root-relative
        // position (which is peer_screen + area, since we've reparented
        // under peer and currently sit at (0,0) in peer).  We query rather
        // than compute from JUCE bounds so the answer is anchored to what
        // X11 actually says, not what we expect.
        int absX = 0, absY = 0;
        Window childReturn = 0;
        if (! X11Symbols::getInstance()->xTranslateCoordinates (
                display, wineX11Window, DefaultRootWindow (display),
                0, 0, &absX, &absY, &childReturn))
            return;

        const WineUINT flags = WINE_SWP_NOACTIVATE
                             | WINE_SWP_NOZORDER
                             | WINE_SWP_NOOWNERZORDER
                             | WINE_SWP_DEFERERASE;

        // (1) Update Wine's WND rect.
        SetWindowPos (hwnd, nullptr, absX, absY, w, h, flags);

        // (2) Pin wine_x11_window back to (0, 0, w, h) within peer's X11
        //     window.  Wine's pSetWindowPos response (XConfigureWindow
        //     with abs coords) would otherwise leave it at the wrong X11
        //     position.
        auto* x = X11Symbols::getInstance();
        x->xMoveResizeWindow (display, wineX11Window, 0, 0, w, h);
        x->xFlush (display);

        lastAbsX    = absX;
        lastAbsY    = absY;
        haveLastAbs = true;
    }

    // Detect host-window moves (Element being dragged across the
    // desktop).  componentMovedOrResized doesn't fire in that case —
    // peer-relative area is unchanged — but absolute screen coords
    // have shifted, so Wine's WND rect now lies again.
    void pollScreenPosition() noexcept
    {
        if (hwnd == nullptr || display == nullptr || wineX11Window == 0)
            return;

        auto* topLevel = owner.getTopLevelComponent();
        if (topLevel == nullptr) return;
        auto* peer = topLevel->getPeer();
        if (peer == nullptr) return;

        int absX = 0, absY = 0;
        Window childReturn = 0;
        if (! X11Symbols::getInstance()->xTranslateCoordinates (
                display, wineX11Window, DefaultRootWindow (display),
                0, 0, &absX, &absY, &childReturn))
            return;

        if (haveLastAbs && absX == lastAbsX && absY == lastAbsY)
            return;

        const auto area = peer->getAreaCoveredBy (owner);
        const int w = jmax (1, area.getWidth());
        const int h = jmax (1, area.getHeight());

        // Same correction dance as in syncHwndScreenPosition — except the
        // bounds haven't actually changed in JUCE-space, only on screen.
        SetWindowPos (hwnd, nullptr, absX, absY, w, h,
                      WINE_SWP_NOACTIVATE | WINE_SWP_NOZORDER
                      | WINE_SWP_NOOWNERZORDER | WINE_SWP_DEFERERASE);

        auto* x = X11Symbols::getInstance();
        x->xMoveResizeWindow (display, wineX11Window, 0, 0, w, h);
        x->xFlush (display);

        lastAbsX    = absX;
        lastAbsY    = absY;
        haveLastAbs = true;
    }

    // ~60 Hz pump.  Drives the Win32 message queue (Wine's x11drv
    // translates X11 events into WM_* messages for our HWND and its
    // plugin children; JUCE's message thread is an X11 poll loop so we
    // need our own PeekMessage loop here) AND polls absolute screen
    // position for host-window-move detection.
    //
    // PeekMessageW with hwnd=NULL pumps the ENTIRE thread queue —
    // critical because the plugin's GUI window is a CHILD HWND of ours
    // and filtering to our HWND alone would skip the plugin's WM_PAINT
    // / mouse / keyboard messages.
    void timerCallback() override
    {
        if (hwnd == nullptr)
            return;

        WineMSG msg;
        while (PeekMessageW (&msg, nullptr, 0, 0, WINE_PM_REMOVE))
        {
            TranslateMessage (&msg);
            DispatchMessageW (&msg);
        }

        pollScreenPosition();
    }

    WineHWNDEmbedComponent& owner;
    WineHWND     hwnd          = nullptr;
    Display*     display       = nullptr;
    Window       wineX11Window = 0;
    ComponentPeer* currentPeer = nullptr;
    int          lastAbsX      = 0;
    int          lastAbsY      = 0;
    bool         haveLastAbs   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

//==============================================================================
WineHWNDEmbedComponent::WineHWNDEmbedComponent (int initialWidth, int initialHeight)
    : pimpl (std::make_unique<Pimpl> (*this, initialWidth, initialHeight))
{
    setSize (initialWidth, initialHeight);
}

WineHWNDEmbedComponent::~WineHWNDEmbedComponent() = default;

void* WineHWNDEmbedComponent::getHWND() const noexcept
{
    return pimpl != nullptr ? (void*) pimpl->getHwnd() : nullptr;
}

void WineHWNDEmbedComponent::updateEmbeddedBounds()
{
    if (pimpl != nullptr)
        pimpl->updateBounds();
}

void WineHWNDEmbedComponent::removeClient()
{
    if (pimpl != nullptr)
        pimpl->removeClient();
}

void WineHWNDEmbedComponent::paint (Graphics&) {}

void WineHWNDEmbedComponent::resized()
{
    if (pimpl != nullptr)
        pimpl->updateBounds();
}

} // namespace juce

#endif // defined(__WINE__) && (JUCE_LINUX || JUCE_BSD)
