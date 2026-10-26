// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// P/Invoke declarations: litemirror.dll (src/sdk/include/litemirror.h) and
// the few user32/gdi32 calls the video surface and the overlay need.
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
            NoH264Decoder,
            Address
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void DeviceCallback(IntPtr user, IntPtr deviceId, IntPtr name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogCallback(IntPtr user, int level, IntPtr message);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void SizeCallback(IntPtr user, IntPtr deviceId, uint width, uint height);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void PinCallback(IntPtr user, IntPtr pin);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void StatsCallback(IntPtr user, IntPtr deviceId, ref Stats stats);

        [StructLayout(LayoutKind.Sequential)]
        public struct Stats
        {
            public uint width, height;
            public float fps, process_ms;
            public int hardware;
        }

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
            public IntPtr on_video_size;
            public IntPtr on_pin;
            public IntPtr on_stats;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Config
        {
            public IntPtr name;   // UTF-8, NUL terminated
            public ushort raop_port;
            public ushort airplay_port;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Colour
        {
            public int matrix, range;
            public float brightness, contrast, saturation, hue;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct AudioDevice
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] id;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] name;
        }

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_create(ref Callbacks cb, out IntPtr server);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_window(IntPtr server, IntPtr hwnd);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_audio(IntPtr server, int enable);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int lm_audio_devices([Out] AudioDevice[] devices, int max);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_audio_device(IntPtr server, byte[] id);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_follow_volume(IntPtr server, int enable);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_pin(IntPtr server, int mode, byte[] pin);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_second_device(IntPtr server, int replace);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_address(IntPtr server, byte[] ipv4);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_display(IntPtr server, uint width, uint height, uint maxFps);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_hw_decode(IntPtr server, int enable);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_colour(IntPtr server, ref Colour colour);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_low_latency(IntPtr server, int enable);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern Error lm_server_set_scaler(IntPtr server, int quality);
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

        public static string Utf8(byte[] buffer)
        {
            int n = Array.IndexOf(buffer, (byte)0);
            return Encoding.UTF8.GetString(buffer, 0, n < 0 ? buffer.Length : n);
        }

        // NUL-terminated UTF-8 for the core; null for an empty string, which every call takes as "default"
        public static byte[] Utf8Bytes(string s)
        {
            return string.IsNullOrEmpty(s) ? null : Encoding.UTF8.GetBytes(s + "\0");
        }

        public static Error Start(IntPtr server, string name, int raopPort, int airplayPort)
        {
            byte[] utf8 = Encoding.UTF8.GetBytes(name + "\0");
            GCHandle pin = GCHandle.Alloc(utf8, GCHandleType.Pinned);
            try
            {
                Config cfg = new Config { name = pin.AddrOfPinnedObject(), raop_port = (ushort)raopPort, airplay_port = (ushort)airplayPort };
                return lm_server_start(server, ref cfg);
            }
            finally
            {
                pin.Free();
            }
        }

        // --- user32 / gdi32 for the child window the video is drawn into and the overlay ---

        public const int WS_CHILD = 0x40000000;
        public const int WS_VISIBLE = 0x10000000;
        public const uint CS_DBLCLKS = 0x0008;
        public const int WM_KEYDOWN = 0x0100;
        public const int WM_SYSKEYDOWN = 0x0104;
        public const int WM_LBUTTONDBLCLK = 0x0203;
        public const int WM_SIZING = 0x0214;
        public const int WM_ENTERSIZEMOVE = 0x0231;
        public const int BLACK_BRUSH = 4;
        public const int GWL_EXSTYLE = -20;
        public const int WS_EX_TRANSPARENT = 0x20;
        public const int WS_EX_TOOLWINDOW = 0x80;
        public const int WS_EX_NOACTIVATE = 0x08000000;
        public const int VK_CONTROL = 0x11;

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT { public int left, top, right, bottom; }

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
        [DllImport("user32.dll")]
        public static extern int GetWindowLong(IntPtr hwnd, int index);
        [DllImport("user32.dll")]
        public static extern int SetWindowLong(IntPtr hwnd, int index, int value);
        [DllImport("user32.dll")]
        public static extern short GetKeyState(int key);
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
