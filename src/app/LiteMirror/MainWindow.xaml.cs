// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.Runtime.InteropServices;
using System.Windows;

namespace LiteMirror
{
    public partial class MainWindow
    {
        IntPtr server;
        // the native side keeps these pointers; the delegates must not be collected
        readonly Native.DeviceCallback onConnected;
        readonly Native.DeviceCallback onDisconnected;
        readonly string receiverName = "LiteMirror (" + Environment.MachineName + ")";

        public MainWindow()
        {
            InitializeComponent();
            onConnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(Native.Utf8(name))));
            onDisconnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(null)));
            Loaded += OnLoaded;
            Closed += OnClosed;
        }

        void OnLoaded(object sender, RoutedEventArgs e)
        {
            ReceiverName.Text = receiverName;
            var cb = new Native.Callbacks
            {
                on_connected = Marshal.GetFunctionPointerForDelegate(onConnected),
                on_disconnected = Marshal.GetFunctionPointerForDelegate(onDisconnected)
            };
            Native.Error err = Native.lm_server_create(ref cb, out server);
            if (err == Native.Error.Ok) err = Native.lm_server_set_window(server, Video.Handle);
            if (err == Native.Error.Ok) err = Native.lm_server_set_audio(server, 1);
            if (err == Native.Error.Ok) err = Native.Start(server, receiverName);
            if (err != Native.Error.Ok)
            {
                HintTitle.Text = "Not running";
                MessageBox.Show(this, Explain(err), "LiteMirror", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        void OnClosed(object sender, EventArgs e)
        {
            Native.lm_server_destroy(server);
            server = IntPtr.Zero;
        }

        void ShowDevice(string name)
        {
            bool connected = name != null;
            Video.Visibility = connected ? Visibility.Visible : Visibility.Hidden;
            Hint.Visibility = connected ? Visibility.Collapsed : Visibility.Visible;
            TitleBar.Title = connected ? "LiteMirror – " + name : "LiteMirror";
            Title = TitleBar.Title;
        }

        static string Explain(Native.Error err)
        {
            switch (err)
            {
                case Native.Error.NoH264Decoder:
                    return "No H.264 video decoder was found.\n\nOn Windows N editions, install the Media Feature Pack (Settings > Apps > Optional features), then start LiteMirror again.";
                case Native.Error.MdnsUnavailable:
                    return "LiteMirror needs Windows 10 version 1809 or later.";
                case Native.Error.MdnsRegister:
                    return "Windows could not announce LiteMirror on the network. Check that the computer is connected to the same Wi-Fi as your phone and that the network is set to Private.";
                case Native.Error.RaopPort:
                case Native.Error.AirplayPort:
                    return "Another program is using the network port LiteMirror needs. Close it and start LiteMirror again.";
                case Native.Error.Internal:
                    return "Direct3D 11 is not available on this computer, so the picture cannot be shown. Update the graphics driver and try again.";
                default:
                    return "LiteMirror could not start: " + Native.Utf8(Native.lm_error_string(err));
            }
        }
    }
}
