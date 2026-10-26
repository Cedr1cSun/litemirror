// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
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
        // "the window is the phone": the phone's screen sets the shape, the user sets the area
        Size screen;            // the phone's screen, learnt from the first picture after connect
        Size picture;           // the last picture size
        bool shaped;            // the window has taken the phone's shape once this connection
        bool turning;           // the turn animation is running
        double dragAspect;      // video-area aspect to hold while the user drags an edge
        readonly DispatcherTimer sizeTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };

        public MainWindow(Settings settings)
        {
            InitializeComponent();
            this.settings = settings;
            onConnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(Native.Utf8(name))));
            onDisconnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(null)));
            onVideoSize = (user, id, w, h) => Dispatcher.BeginInvoke(new Action(() => OnPictureSize(w, h)));
            Topmost = settings.AlwaysOnTop;
            sizeTimer.Tick += OnSizeTimer;
            SourceInitialized += (s, e) => HwndSource.FromHwnd(new WindowInteropHelper(this).Handle).AddHook(Hook);
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
            if (!picture.IsEmpty) sizeTimer.Start();   // pictures that arrived while hidden
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
            if (!connected)
            {
                screen = picture = Size.Empty;
                shaped = false;
                sizeTimer.Stop();
            }
            Video.Visibility = connected ? Visibility.Visible : Visibility.Hidden;
            Hint.Visibility = connected ? Visibility.Collapsed : Visibility.Visible;
            ApplyStrings();
        }

        // iOS sends a few sizes in a row when an app goes full screen: act on the last one
        void OnPictureSize(uint width, uint height)
        {
            if (width == 0 || height == 0) return;
            picture = new Size(width, height);
            // shortcut: a phone that connects while playing a video is learnt at the video's shape, until it reconnects
            if (screen.IsEmpty) screen = picture;
            sizeTimer.Stop();
            sizeTimer.Start();
        }

        void OnSizeTimer(object sender, EventArgs e)
        {
            if (turning) return;   // tick again once the turn has finished
            sizeTimer.Stop();
            if (!CanPlace() || picture.IsEmpty) return;
            // the first picture always shapes the window; later ones only if the user wants it to turn
            if (shaped && !settings.FollowRotation) return;
            Rect? to = WindowShape.ForPicture(Bounds, Chrome, SystemParameters.WorkArea, screen, picture);
            shaped = true;
            if (to.HasValue) MoveTo(to.Value);
        }

        // Ctrl+F: the picture at its own pixel size
        void FitToPicture()
        {
            if (!CanPlace() || picture.IsEmpty || turning) return;
            double dpi = VisualTreeHelper.GetDpi(this).DpiScaleX;
            MoveTo(WindowShape.Fit(Bounds, Chrome, SystemParameters.WorkArea, new Size(picture.Width / dpi, picture.Height / dpi)));
        }

        bool CanPlace() => !fullScreen && WindowState == WindowState.Normal && IsVisible && Video.ActualWidth > 0;
        Rect Bounds => new Rect(Left, Top, ActualWidth, ActualHeight);
        Size Chrome => new Size(ActualWidth - Video.ActualWidth, ActualHeight - Video.ActualHeight);

        // Turn the window over 200 ms. The base values are set first and the animations
        // stop at the end, so the properties stay free for the user to drag afterwards.
        void MoveTo(Rect to)
        {
            turning = true;
            Rect from = Bounds;
            Left = to.Left; Top = to.Top; Width = to.Width; Height = to.Height;
            Animate(LeftProperty, from.Left, to.Left);
            Animate(TopProperty, from.Top, to.Top);
            Animate(WidthProperty, from.Width, to.Width);
            Animate(HeightProperty, from.Height, to.Height, (s, e) => turning = false);
        }

        void Animate(DependencyProperty property, double from, double to, EventHandler done = null)
        {
            var a = new DoubleAnimation(from, to, TimeSpan.FromMilliseconds(200))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
                FillBehavior = FillBehavior.Stop
            };
            if (done != null) a.Completed += done;
            BeginAnimation(property, a);
        }

        // While a phone is connected, dragging an edge keeps the window's shape
        IntPtr Hook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == Native.WM_ENTERSIZEMOVE)
            {
                dragAspect = shaped ? Video.ActualWidth / Video.ActualHeight : 0;
            }
            else if (msg == Native.WM_SIZING && dragAspect > 0)
            {
                // the rect is in device pixels; the chrome is measured in DIPs
                var rc = (Native.RECT)Marshal.PtrToStructure(lParam, typeof(Native.RECT));
                double dpi = VisualTreeHelper.GetDpi(this).DpiScaleX;
                Size chrome = Chrome;
                Rect r = WindowShape.KeepAspect(new Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top),
                    (int)wParam, new Size(chrome.Width * dpi, chrome.Height * dpi), dragAspect);
                rc.left = (int)Math.Round(r.Left);
                rc.top = (int)Math.Round(r.Top);
                rc.right = (int)Math.Round(r.Right);
                rc.bottom = (int)Math.Round(r.Bottom);
                Marshal.StructureToPtr(rc, lParam, false);
                handled = true;
                return (IntPtr)1;
            }
            return IntPtr.Zero;
        }

        void OnVideoDoubleClick(object sender, EventArgs e) => ToggleFullScreen();

        void OnKey(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.F11 || (e.Key == Key.Escape && fullScreen))
            {
                ToggleFullScreen();
                e.Handled = true;
            }
            else if (e.Key == Key.F && Keyboard.Modifiers == ModifierKeys.Control)
            {
                FitToPicture();
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
