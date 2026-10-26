// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// A plain Win32 child window inside the WPF tree. litemirror.dll owns its
// swap chain and draws every frame into it; WPF only places and sizes it.
// Mouse and keyboard input land on this window, not on WPF, so the
// double-click and the shortcut keys are picked up here and handed on.
using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;

namespace LiteMirror
{
    public class VideoHost : HwndHost
    {
        const string ClassName = "LiteMirrorVideo";
        static bool registered;

        public event EventHandler DoubleClick;
        // the left button went down on the picture: a window without a title bar is dragged by it
        public event EventHandler Drag;
        // the right button came up on the picture: its menu
        public event EventHandler Menu;
        // a key went down while the video had the focus: the virtual-key code
        public event Action<int> KeyPressed;

        protected override HandleRef BuildWindowCore(HandleRef parent)
        {
            if (!registered)
            {
                // DefWindowProc as the window procedure: the window only needs a black background
                var wc = new Native.WNDCLASSEX
                {
                    cbSize = (uint)Marshal.SizeOf(typeof(Native.WNDCLASSEX)),
                    style = Native.CS_DBLCLKS,
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

        protected override IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == Native.WM_LBUTTONDBLCLK)
            {
                DoubleClick?.Invoke(this, EventArgs.Empty);
                handled = true;
            }
            else if (msg == Native.WM_LBUTTONDOWN)
            {
                Drag?.Invoke(this, EventArgs.Empty);
            }
            else if (msg == Native.WM_RBUTTONUP)
            {
                Menu?.Invoke(this, EventArgs.Empty);
                handled = true;
            }
            else if (msg == Native.WM_KEYDOWN || msg == Native.WM_SYSKEYDOWN)
            {
                KeyPressed?.Invoke((int)wParam);
                handled = true;
            }
            return base.WndProc(hwnd, msg, wParam, lParam, ref handled);
        }
    }
}
