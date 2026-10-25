// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// A plain Win32 child window inside the WPF tree. litemirror.dll owns its
// swap chain and draws every frame into it; WPF only places and sizes it.
using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;

namespace LiteMirror
{
    public class VideoHost : HwndHost
    {
        const string ClassName = "LiteMirrorVideo";
        static bool registered;

        protected override HandleRef BuildWindowCore(HandleRef parent)
        {
            if (!registered)
            {
                // DefWindowProc as the window procedure: the window only needs a black background
                var wc = new Native.WNDCLASSEX
                {
                    cbSize = (uint)Marshal.SizeOf(typeof(Native.WNDCLASSEX)),
                    lpfnWndProc = Native.GetProcAddress(Native.LoadLibrary("user32.dll"), "DefWindowProcW"),
                    hInstance = Native.GetModuleHandle(null),
                    hbrBackground = Native.GetStockObject(Native.BLACK_BRUSH),
                    lpszClassName = ClassName
                };
                registered = Native.RegisterClassEx(ref wc) != 0;
            }
            IntPtr hwnd = Native.CreateWindowEx(0, ClassName, "", Native.WS_CHILD | Native.WS_VISIBLE,
                0, 0, 1, 1, parent.Handle, IntPtr.Zero, Native.GetModuleHandle(null), IntPtr.Zero);
            if (hwnd == IntPtr.Zero)
            {
                throw new InvalidOperationException("Could not create the video window: error " + Marshal.GetLastWin32Error());
            }
            return new HandleRef(this, hwnd);
        }

        protected override void DestroyWindowCore(HandleRef hwnd)
        {
            Native.DestroyWindow(hwnd.Handle);
        }
    }
}
