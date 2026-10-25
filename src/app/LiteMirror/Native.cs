// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// P/Invoke declarations: litemirror.dll (src/sdk/include/litemirror.h) and
// the few user32/gdi32 calls the video surface needs.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LiteMirror
{
    internal static class Native
    {
        const string Dll = "litemirror.dll";

        public enum Error
        {
            Ok = 0,
            InvalidArg,
            Running,
            NoMemory,
            RaopPort,
            AirplayPort,
            MdnsUnavailable,
            MdnsRegister,
            Internal,
            NoH264Decoder
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void DeviceCallback(IntPtr user, IntPtr deviceId, IntPtr name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogCallback(IntPtr user, int level, IntPtr message);

        // Mirrors lm_callbacks: function pointers so that unused members stay NULL
        [StructLayout(LayoutKind.Sequential)]
        public struct Callbacks
        {
            public IntPtr user;
            public IntPtr on_connected;
            public IntPtr on_disconnected;
            public IntPtr on_video;
            public IntPtr on_audio;
            public IntPtr on_volume;
            public IntPtr on_metadata;
            public IntPtr on_cover_art;
            public IntPtr on_play_url;
            public IntPtr on_log;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Config
        {
            public IntPtr name;   // UTF-8, NUL terminated
            public ushort raop_port;
            public ushort airplay_port;
        }

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_create(ref Callbacks cb, out IntPtr server);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_window(IntPtr server, IntPtr hwnd);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_audio(IntPtr server, int enable);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_start(IntPtr server, ref Config cfg);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void lm_server_stop(IntPtr server);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void lm_server_destroy(IntPtr server);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr lm_error_string(Error e);

        // The core hands out UTF-8; the marshaller's default is the ANSI code page
        public static string Utf8(IntPtr p)
        {
            if (p == IntPtr.Zero) return "";
            int n = 0;
            while (Marshal.ReadByte(p, n) != 0) n++;
            byte[] bytes = new byte[n];
            Marshal.Copy(p, bytes, 0, n);
            return Encoding.UTF8.GetString(bytes);
        }

        public static Error Start(IntPtr server, string name)
        {
            byte[] utf8 = Encoding.UTF8.GetBytes(name + "\0");
            GCHandle pin = GCHandle.Alloc(utf8, GCHandleType.Pinned);
            try
            {
                Config cfg = new Config { name = pin.AddrOfPinnedObject() };   // ports 0: the OS picks
                return lm_server_start(server, ref cfg);
            }
            finally
            {
                pin.Free();
            }
        }

        // --- user32 / gdi32 for the child window the video is drawn into ---

        public const int WS_CHILD = 0x40000000;
        public const int WS_VISIBLE = 0x10000000;
        public const int BLACK_BRUSH = 4;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct WNDCLASSEX
        {
            public uint cbSize;
            public uint style;
            public IntPtr lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public IntPtr hInstance;
            public IntPtr hIcon;
            public IntPtr hCursor;
            public IntPtr hbrBackground;
            public string lpszMenuName;
            public string lpszClassName;
            public IntPtr hIconSm;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern ushort RegisterClassEx(ref WNDCLASSEX wc);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr CreateWindowEx(int exStyle, string className, string windowName, int style,
            int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
        [DllImport("user32.dll")]
        public static extern bool DestroyWindow(IntPtr hwnd);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr GetModuleHandle(string module);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        public static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr LoadLibrary(string name);
        [DllImport("gdi32.dll")]
        public static extern IntPtr GetStockObject(int obj);
    }
}
