// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using Forms = System.Windows.Forms;

namespace LiteMirror
{
    public partial class MainWindow
    {
        IntPtr server;
        // the native side keeps these pointers; the delegates must not be collected
        readonly Native.DeviceCallback onConnected;
        readonly Native.DeviceCallback onDisconnected;
        readonly Native.SizeCallback onVideoSize;
        Settings settings;
        string deviceName;      // connected phone, or null
        bool running;
        Forms.NotifyIcon tray;
        bool quitting;
        // full screen: the window is sized to the monitor by hand (no WindowState games with the custom chrome)
        bool fullScreen;
        Rect savedBounds;
        WindowState savedState;

        public MainWindow(Settings settings)
        {
            InitializeComponent();
            this.settings = settings;
            onConnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(Native.Utf8(name))));
            onDisconnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(null)));
            onVideoSize = (user, id, w, h) => Dispatcher.BeginInvoke(new Action(() => FitToPicture(w, h)));
            Topmost = settings.AlwaysOnTop;
            Loaded += OnLoaded;
            Closing += OnClosing;
            Closed += OnClosed;
            PreviewKeyDown += OnKey;
        }

        void OnLoaded(object sender, RoutedEventArgs e)
        {
            SetupTray();
            var cb = new Native.Callbacks
            {
                on_connected = Marshal.GetFunctionPointerForDelegate(onConnected),
                on_disconnected = Marshal.GetFunctionPointerForDelegate(onDisconnected),
                on_video_size = Marshal.GetFunctionPointerForDelegate(onVideoSize)
            };
            Native.Error err = Native.lm_server_create(ref cb, out server);
            if (err == Native.Error.Ok) err = Native.lm_server_set_window(server, Video.Handle);
            if (err == Native.Error.Ok) err = Native.lm_server_set_audio(server, 1);
            if (err == Native.Error.Ok) err = Start();
            ApplyStrings();
            if (err != Native.Error.Ok)
            {
                MessageBox.Show(this, Explain(err), Strings.Get("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        Native.Error Start()
        {
            if (settings.Name.Length == 0)
            {
                settings.Name = Strings.Get("AppName") + " (" + Environment.MachineName + ")";
            }
            Native.Error err = Native.Start(server, settings.Name);
            running = err == Native.Error.Ok;
            return err;
        }

        // Every piece of text, so a language change in Settings applies at once
        void ApplyStrings()
        {
            string app = Strings.Get("AppName");
            Title = TitleBar.Title = deviceName != null ? app + " – " + deviceName : app;
            HintTitle.Text = Strings.Get(running ? "Ready" : "NotRunning");
            HintBody.Text = Strings.Get("HintBody");
            ReceiverName.Text = settings.Name;
            SettingsButton.ToolTip = Strings.Get("Settings");
            tray.Text = app;
            tray.ContextMenuStrip.Items[0].Text = Strings.Get("TrayOpen");
            tray.ContextMenuStrip.Items[1].Text = Strings.Get("TraySettings");
            tray.ContextMenuStrip.Items[3].Text = Strings.Get("TrayQuit");
        }

        void SetupTray()
        {
            var menu = new Forms.ContextMenuStrip();
            menu.Items.Add("", null, (s, e) => ShowFromTray());
            menu.Items.Add("", null, (s, e) => { ShowFromTray(); OpenSettings(s, null); });
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("", null, (s, e) => { quitting = true; Close(); });
            tray = new Forms.NotifyIcon
            {
                Icon = System.Drawing.Icon.ExtractAssociatedIcon(System.Reflection.Assembly.GetExecutingAssembly().Location),
                ContextMenuStrip = menu,
                Visible = true
            };
            tray.MouseClick += (s, e) => { if (e.Button == Forms.MouseButtons.Left) ShowFromTray(); };
        }

        void ShowFromTray()
        {
            Show();
            if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
            Activate();
        }

        // Closing the window keeps receiving in the tray; Quit in the tray menu really exits
        void OnClosing(object sender, CancelEventArgs e)
        {
            if (quitting) return;
            e.Cancel = true;
            if (fullScreen) ToggleFullScreen();
            Hide();
        }

        void OnClosed(object sender, EventArgs e)
        {
            Native.lm_server_destroy(server);
            server = IntPtr.Zero;
            tray.Visible = false;
            tray.Dispose();
        }

        void ShowDevice(string name)
        {
            deviceName = name;
            bool connected = name != null;
            Video.Visibility = connected ? Visibility.Visible : Visibility.Hidden;
            Hint.Visibility = connected ? Visibility.Collapsed : Visibility.Visible;
            ApplyStrings();
        }

        // Resize the window so the video area has the picture's aspect ratio: the
        // height stays, the width follows, both shrink if the work area is too small
        void FitToPicture(uint width, uint height)
        {
            if (fullScreen || WindowState != WindowState.Normal || width == 0 || height == 0 || !IsVisible) return;
            double areaW = Video.ActualWidth, areaH = Video.ActualHeight;
            if (areaW <= 0 || areaH <= 0) return;
            Rect work = SystemParameters.WorkArea;
            double chromeW = ActualWidth - areaW, chromeH = ActualHeight - areaH;
            double newH = areaH, newW = areaH * width / height;
            if (newW + chromeW > work.Width) { newW = work.Width - chromeW; newH = newW * height / width; }
            if (newH + chromeH > work.Height) { newH = work.Height - chromeH; newW = newH * width / height; }
            Width = newW + chromeW;
            Height = newH + chromeH;
            Left = Math.Max(work.Left, Math.Min(Left, work.Right - Width));
            Top = Math.Max(work.Top, Math.Min(Top, work.Bottom - Height));
        }

        void OnVideoDoubleClick(object sender, EventArgs e) => ToggleFullScreen();

        void OnKey(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.F11 || (e.Key == Key.Escape && fullScreen))
            {
                ToggleFullScreen();
                e.Handled = true;
            }
        }

        void ToggleFullScreen()
        {
            fullScreen = !fullScreen;
            if (fullScreen)
            {
                savedState = WindowState;
                savedBounds = new Rect(Left, Top, Width, Height);
                WindowState = WindowState.Normal;
                TitleBar.Visibility = Visibility.Collapsed;
                ResizeMode = ResizeMode.NoResize;
                WindowCornerPreference = Wpf.Ui.Controls.WindowCornerPreference.DoNotRound;
                // the monitor the window is on, in device-independent units
                var screen = Forms.Screen.FromHandle(new WindowInteropHelper(this).Handle).Bounds;
                var toDip = PresentationSource.FromVisual(this).CompositionTarget.TransformFromDevice;
                Point origin = toDip.Transform(new Point(screen.Left, screen.Top));
                Point extent = toDip.Transform(new Point(screen.Width, screen.Height));
                Left = origin.X;
                Top = origin.Y;
                Width = extent.X;
                Height = extent.Y;
            }
            else
            {
                TitleBar.Visibility = Visibility.Visible;
                ResizeMode = ResizeMode.CanResize;
                WindowCornerPreference = Wpf.Ui.Controls.WindowCornerPreference.Round;
                Left = savedBounds.Left;
                Top = savedBounds.Top;
                Width = savedBounds.Width;
                Height = savedBounds.Height;
                WindowState = savedState;
            }
        }

        void OpenSettings(object sender, RoutedEventArgs e)
        {
            var dialog = new SettingsWindow(settings) { Owner = this };
            if (dialog.ShowDialog() != true) return;
            bool rename = dialog.Result.Name != settings.Name;
            bool relanguage = dialog.Result.Language != settings.Language;
            settings = dialog.Result;
            Topmost = settings.AlwaysOnTop;
            if (relanguage) Strings.Use(settings.Language);
            Native.Error err = Native.Error.Ok;
            if (rename && server != IntPtr.Zero)
            {
                Native.lm_server_stop(server);   // the name is announced at start, so start again
                err = Start();
            }
            ApplyStrings();
            if (err != Native.Error.Ok)
            {
                MessageBox.Show(this, Explain(err), Strings.Get("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        static string Explain(Native.Error err)
        {
            switch (err)
            {
                case Native.Error.NoH264Decoder:   return Strings.Get("ErrNoDecoder");
                case Native.Error.MdnsUnavailable: return Strings.Get("ErrMdnsUnavailable");
                case Native.Error.MdnsRegister:    return Strings.Get("ErrMdnsRegister");
                case Native.Error.RaopPort:
                case Native.Error.AirplayPort:     return Strings.Get("ErrPort");
                case Native.Error.Internal:        return Strings.Get("ErrNoD3D");
                default: return string.Format(Strings.Get("ErrOther"), Native.Utf8(Native.lm_error_string(err)));
            }
        }
    }
}
