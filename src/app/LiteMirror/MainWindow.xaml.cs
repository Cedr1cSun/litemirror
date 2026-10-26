// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using HandyControl.Controls;
using Forms = System.Windows.Forms;

namespace LiteMirror
{
    public partial class MainWindow
    {
        // text next to the sidebar icons: hidden while the sidebar is an icon rail
        public static readonly DependencyProperty NavTextProperty = DependencyProperty.Register(
            "NavText", typeof(Visibility), typeof(MainWindow), new PropertyMetadata(Visibility.Visible));
        public Visibility NavText
        {
            get => (Visibility)GetValue(NavTextProperty);
            set => SetValue(NavTextProperty, value);
        }

        const double SideWide = 168, SideRail = 50;

        IntPtr server;
        // the native side keeps these pointers; the delegates must not be collected
        readonly Native.DeviceCallback onConnected;
        readonly Native.DeviceCallback onDisconnected;
        readonly Native.SizeCallback onVideoSize;
        readonly Native.LogCallback onLog;
        readonly Native.PinCallback onPin;
        readonly Native.StatsCallback onStats;
        readonly Native.SecondDeviceCallback onSecondDevice;
        readonly Settings settings;
        string deviceName;      // connected phone, or null
        bool running;
        bool videoReady;        // the window sink opened
        Forms.NotifyIcon tray;
        Forms.ToolStripMenuItem trayOpen, trayStart, traySettings, trayQuit;
        bool quitting;
        OsdWindow osd;
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
        // receiver settings changed: restart once the burst of changes is over, or once the phone has left
        readonly DispatcherTimer restartTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
        bool restartWhenFree;

        public MainWindow(Settings settings)
        {
            InitializeComponent();
            this.settings = settings;
            onConnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(Native.Utf8(name))));
            onDisconnected = (user, id, name) => Dispatcher.BeginInvoke(new Action(() => ShowDevice(null)));
            onVideoSize = (user, id, w, h) => Dispatcher.BeginInvoke(new Action(() => OnPictureSize(w, h)));
            onLog = (user, level, msg) => Log.Write(level, Native.Utf8(msg));
            onPin = (user, pin) => { string code = pin == IntPtr.Zero ? null : Native.Utf8(pin); Dispatcher.BeginInvoke(new Action(() => ShowPin(code))); };
            onStats = (IntPtr user, IntPtr id, ref Native.Stats stats) => { Native.Stats copy = stats; Dispatcher.BeginInvoke(new Action(() => ShowStats(copy))); };
            onSecondDevice = (user, id, name) => AskSecondDevice(Native.Utf8(name));
            Topmost = settings.AlwaysOnTop;
            PinButton.IsChecked = settings.AlwaysOnTop;
            sizeTimer.Tick += OnSizeTimer;
            restartTimer.Tick += OnRestartTimer;
            SourceInitialized += (s, e) => HwndSource.FromHwnd(new WindowInteropHelper(this).Handle).AddHook(Hook);
            Loaded += OnLoaded;
            Closing += OnClosing;
            Closed += OnClosed;
            PreviewKeyDown += (s, e) => { if (Shortcut(e.Key, Keyboard.Modifiers.HasFlag(ModifierKeys.Control))) e.Handled = true; };
            // keys pressed while the Win32 video window has the focus
            Video.KeyPressed += vk => Shortcut(KeyInterop.KeyFromVirtualKey(vk), (Native.GetKeyState(Native.VK_CONTROL) & 0x8000) != 0);
            LocationChanged += (s, e) => PlaceOsd();
            SizeChanged += (s, e) => PlaceOsd();
            StateChanged += (s, e) => UpdateOsd();
            IsVisibleChanged += (s, e) => UpdateOsd();
            NewSettingsPage();
        }

        void OnLoaded(object sender, RoutedEventArgs e)
        {
            SetupTray();
            var cb = new Native.Callbacks
            {
                on_connected = Marshal.GetFunctionPointerForDelegate(onConnected),
                on_disconnected = Marshal.GetFunctionPointerForDelegate(onDisconnected),
                on_log = Marshal.GetFunctionPointerForDelegate(onLog),
                on_video_size = Marshal.GetFunctionPointerForDelegate(onVideoSize),
                on_pin = Marshal.GetFunctionPointerForDelegate(onPin),
                on_stats = Marshal.GetFunctionPointerForDelegate(onStats),
                on_second_device = Marshal.GetFunctionPointerForDelegate(onSecondDevice)
            };
            Native.Error err = Native.lm_server_create(ref cb, out server);
            if (err == Native.Error.Ok)
            {
                err = Native.lm_server_set_window(server, Video.Handle);
                videoReady = err == Native.Error.Ok;
                Native.lm_server_set_scaler(server, settings.Scaler);
            }
            ApplyStrings();
            if (err != Native.Error.Ok)
            {
                Fail(err);
            }
            else if (settings.AutoStart)
            {
                StartReceiver();
            }
            if (settings.CheckUpdates)
            {
                CheckUpdate(true);
            }
        }

        // ---- the receiver ----

        string ReceiverName() => settings.Name.Length == 0 ? Environment.MachineName : settings.Name;

        void StartReceiver()
        {
            if (server == IntPtr.Zero || running) return;
            // everything the core reads before it starts
            int pinMode = settings.PinMode == 2 && settings.Pin.Length == 0 ? 0 : settings.PinMode;
            Native.lm_server_set_pin(server, pinMode, Native.Utf8Bytes(settings.Pin));
            Native.lm_server_set_second_device(server, settings.SecondDevice);
            Native.lm_server_set_address(server, Native.Utf8Bytes(settings.Address));
            Native.lm_server_set_display(server, (uint)(settings.Quality * 16 / 9), (uint)settings.Quality, (uint)settings.Fps);
            Native.lm_server_set_hw_decode(server, settings.HwDecode ? 1 : 0);
            Native.lm_server_set_low_latency(server, settings.LowLatency ? 1 : 0);
            Native.lm_server_set_audio(server, settings.AudioEnabled ? 1 : 0);
            Native.lm_server_set_audio_device(server, Native.Utf8Bytes(settings.AudioDevice));
            Native.lm_server_set_follow_volume(server, settings.FollowVolume ? 1 : 0);
            ApplyColour();

            Native.Error err = Native.Start(server, ReceiverName(), settings.RaopPort, settings.AirplayPort);
            if (err == Native.Error.Address)
            {
                // the chosen adapter is gone (hotspot off, cable out): listen everywhere rather than not at all
                Native.lm_server_set_address(server, null);
                err = Native.Start(server, ReceiverName(), settings.RaopPort, settings.AirplayPort);
                if (err == Native.Error.Ok) Growl.Warning(Strings.Get("Msg_AddressFallback"));
            }
            running = err == Native.Error.Ok;
            Log.Info("start receiving as \"" + ReceiverName() + "\": " + err);
            UpdateStatus();
            if (err != Native.Error.Ok) Fail(err);
        }

        void StopReceiver()
        {
            if (server == IntPtr.Zero || !running) return;
            Native.lm_server_stop(server);   // no callback fires after this returns
            running = false;
            Log.Info("stop receiving");
            ShowPin(null);
            ShowDevice(null);
        }

        void ToggleReceiver()
        {
            if (running) StopReceiver();
            else StartReceiver();
        }

        void ApplyColour()
        {
            if (server == IntPtr.Zero) return;
            var c = new Native.Colour
            {
                matrix = settings.Matrix,
                range = settings.Range,
                brightness = settings.Brightness / 100f,
                contrast = settings.Contrast / 100f,
                saturation = settings.Saturation / 100f,
                hue = settings.Hue / 100f
            };
            Native.lm_server_set_colour(server, ref c);
        }

        void Fail(Native.Error err)
        {
            Log.Error("start failed: " + err);
            Growl.Error(Explain(err));
        }

        // ---- settings ----

        void NewSettingsPage()
        {
            var page = new SettingsPage(settings);
            page.Changed += OnSettingChanged;
            SettingsHost.Content = page;
        }

        void OnSettingChanged(string field)
        {
            switch (field)
            {
                case "Theme":
                case "ThemeColour":
                    App.ApplyTheme(settings);
                    break;
                case "Language":
                    Strings.Use(settings.Language);
                    ApplyStrings();
                    NewSettingsPage();   // its texts were read at load
                    break;
                case "AlwaysOnTop":
                case "AutoStart":
                case "FollowRotation":
                case "FullscreenMonitor":
                case "CloseToTray":
                    break;
                case "ShowInfo":
                    InfoButton.IsChecked = settings.ShowInfo;
                    UpdateOsd();
                    break;
                case "Colour":
                    ApplyColour();
                    break;
                case "Scaler":
                    Native.lm_server_set_scaler(server, settings.Scaler);
                    break;
                case "LogLevel":
                    Log.Level = settings.LogLevel;
                    break;
                default:
                    ScheduleRestart();   // the core reads it at start
                    break;
            }
        }

        void ScheduleRestart()
        {
            if (!running) return;
            if (deviceName != null)
            {
                if (!restartWhenFree) Growl.Info(Strings.Get("Msg_AppliesLater"));
                restartWhenFree = true;
                return;
            }
            restartTimer.Stop();
            restartTimer.Start();
        }

        void OnRestartTimer(object sender, EventArgs e)
        {
            restartTimer.Stop();
            if (!running || deviceName != null) return;
            StopReceiver();
            StartReceiver();
        }

        // ---- text, status, tray ----

        // Every piece of text, so a language change applies at once
        void ApplyStrings()
        {
            NavMirror.Content = Strings.Get("Nav_Mirror");
            NavSettings.Content = Strings.Get("Nav_Settings");
            NavAbout.Content = Strings.Get("Nav_About");
            PinButton.ToolTip = Strings.Get("Tip_Pin");
            FitButton.ToolTip = Strings.Get("Tip_Fit");
            FullButton.ToolTip = Strings.Get("Tip_Full");
            InfoButton.ToolTip = Strings.Get("Tip_Info");
            PinTitle.Text = Strings.Get("Pin_Title");
            AboutVersion.Text = string.Format(Strings.Get("Version"), App.Version);
            AboutDesc.Text = Strings.Get("About_Desc");
            AboutLicence.Text = Strings.Get("About_Licence");
            SiteButton.Content = Strings.Get("About_Site");
            UpdateButton.Content = Strings.Get("Update_Check");
            DownloadButton.Content = Strings.Get("Update_Download");
            LogsButton.Content = Strings.Get("OpenLogs");
            trayOpen.Text = Strings.Get("Tray_Open");
            traySettings.Text = Strings.Get("Tray_Settings");
            trayQuit.Text = Strings.Get("Tray_Quit");
            UpdateStatus();
        }

        void UpdateStatus()
        {
            string app = Strings.Get("AppName");
            Title = deviceName != null ? app + " – " + deviceName : app;
            tray.Text = Title.Length < 64 ? Title : app;
            StatusDot.Fill = (Brush)FindResource(running ? "SuccessBrush" : "ThirdlyTextBrush");
            StatusText.Text = Strings.Get(running ? "Status_On" : "Status_Off");
            StartButton.Content = Strings.Get(running ? "Stop" : "Start");
            StartButton.Style = (Style)FindResource(running ? "ButtonDefault" : "ButtonPrimary");
            trayStart.Text = StartButton.Content.ToString();
            HintTitle.Text = Strings.Get(running ? "Hint_Ready" : "Hint_Stopped");
            HintBody.Text = Strings.Get(running ? "Hint_Body" : "Hint_StoppedBody");
            ReceiverLabel.Text = running ? ReceiverName() : "";
            DeviceLabel.Text = deviceName ?? "";
        }

        void SetupTray()
        {
            var menu = new Forms.ContextMenuStrip();
            trayOpen = new Forms.ToolStripMenuItem("", null, (s, e) => ShowFromTray());
            trayStart = new Forms.ToolStripMenuItem("", null, (s, e) => ToggleReceiver());
            traySettings = new Forms.ToolStripMenuItem("", null, (s, e) => { ShowFromTray(); NavSettings.IsChecked = true; });
            trayQuit = new Forms.ToolStripMenuItem("", null, (s, e) => { quitting = true; Close(); });
            menu.Items.Add(trayOpen);
            menu.Items.Add(trayStart);
            menu.Items.Add(traySettings);
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add(trayQuit);
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

        // Closing the window quits, unless the user asked for the tray; Quit in the tray menu always exits
        void OnClosing(object sender, CancelEventArgs e)
        {
            if (quitting || !settings.CloseToTray) return;
            e.Cancel = true;
            if (fullScreen) ToggleFullScreen();
            Hide();
        }

        void OnClosed(object sender, EventArgs e)
        {
            osd?.Close();
            Native.lm_server_destroy(server);
            server = IntPtr.Zero;
            tray.Visible = false;
            tray.Dispose();
        }

        // ---- pages and the sidebar ----

        void OnNav(object sender, RoutedEventArgs e)
        {
            if (MirrorPage == null || AboutPage == null) return;   // fired while the XAML is still loading
            ShowPage(sender == NavMirror ? MirrorPage : sender == NavSettings ? (UIElement)SettingsHost : AboutPage);
        }

        void ShowPage(UIElement page)
        {
            foreach (UIElement p in new UIElement[] { MirrorPage, SettingsHost, AboutPage })
            {
                p.Visibility = p == page ? Visibility.Visible : Visibility.Collapsed;
            }
            page.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(160)) { EasingFunction = new CubicEase() });
            UpdateOsd();
        }

        // Connected: the sidebar becomes an icon rail so the picture gets the room
        void Rail(bool on)
        {
            NavText = on ? Visibility.Collapsed : Visibility.Visible;
            var a = new DoubleAnimation(on ? SideRail : SideWide, TimeSpan.FromMilliseconds(200)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
            Sidebar.BeginAnimation(WidthProperty, a);
        }

        void ShowDevice(string name)
        {
            deviceName = name;
            bool connected = name != null;
            Video.Visibility = connected ? Visibility.Visible : Visibility.Hidden;
            Hint.Visibility = connected ? Visibility.Collapsed : Visibility.Visible;
            Toolbar.Visibility = connected ? Visibility.Visible : Visibility.Collapsed;
            if (connected)
            {
                NavMirror.IsChecked = true;
                ShowPin(null);
            }
            else
            {
                screen = picture = Size.Empty;
                shaped = false;
                sizeTimer.Stop();
                if (restartWhenFree)
                {
                    restartWhenFree = false;
                    restartTimer.Start();
                }
            }
            if (!fullScreen) Rail(connected);
            UpdateStatus();
            UpdateOsd();
        }

        // Another phone wants in while one is mirroring: asked on a network thread, answered
        // by the user in a dialog; the phone waits, so no answer within the dialog's countdown
        // (or the UI being stuck) counts as no.
        int AskSecondDevice(string newcomer)
        {
            try
            {
                return (int)Dispatcher.Invoke(DispatcherPriority.Normal, TimeSpan.FromSeconds(20), new Func<int>(() =>
                {
                    if (!IsVisible) ShowFromTray();
                    var dialog = new SecondDeviceDialog(this, newcomer, deviceName ?? "");
                    return dialog.ShowDialog() == true ? 1 : 0;
                }));
            }
            catch (TimeoutException)
            {
                return 0;
            }
        }

        void ShowPin(string code)
        {
            PinCard.Visibility = code != null ? Visibility.Visible : Visibility.Collapsed;
            PinCode.Text = code ?? "";
        }

        // ---- the picture information overlay ----

        void ShowStats(Native.Stats s)
        {
            if (osd == null) return;
            osd.Text = string.Format("{0}×{1}  ·  {2:0} fps  ·  {3:0.0} ms  ·  {4}", s.width, s.height, s.fps, s.process_ms, s.hardware != 0 ? "GPU" : "CPU");
        }

        void UpdateOsd()
        {
            bool show = settings.ShowInfo && deviceName != null && videoReady && IsVisible && WindowState != WindowState.Minimized && MirrorPage.Visibility == Visibility.Visible;
            if (show)
            {
                if (osd == null)
                {
                    osd = new OsdWindow(this) { Text = "…" };
                }
                osd.Show();
                PlaceOsd();
            }
            else
            {
                osd?.Hide();
            }
        }

        void PlaceOsd()
        {
            if (osd != null && osd.IsVisible) osd.Follow(Video);
        }

        // ---- shortcuts and title bar actions ----

        bool Shortcut(Key key, bool ctrl)
        {
            if (key == Key.F11 || (key == Key.Escape && fullScreen)) ToggleFullScreen();
            else if (ctrl && key == Key.F) FitToPicture();
            else if (ctrl && key == Key.I) ToggleInfo();
            else if (ctrl && key == Key.R) ToggleReceiver();
            else return false;
            return true;
        }

        void ToggleInfo()
        {
            settings.ShowInfo = !settings.ShowInfo;
            settings.Save();
            InfoButton.IsChecked = settings.ShowInfo;
            ((SettingsPage)SettingsHost.Content).Load();
            UpdateOsd();
        }

        void OnPin(object sender, RoutedEventArgs e)
        {
            settings.AlwaysOnTop = PinButton.IsChecked == true;
            settings.Save();
            Topmost = settings.AlwaysOnTop;
        }

        void OnFit(object sender, RoutedEventArgs e) => FitToPicture();
        void OnFullScreen(object sender, RoutedEventArgs e) => ToggleFullScreen();
        void OnInfo(object sender, RoutedEventArgs e) => ToggleInfo();
        void OnStartStop(object sender, RoutedEventArgs e) => ToggleReceiver();
        void OnVideoDoubleClick(object sender, EventArgs e) => ToggleFullScreen();
        void OnSite(object sender, RoutedEventArgs e) => Process.Start("https://github.com/Cedr1cSun/litemirror");
        void OnOpenLogs(object sender, RoutedEventArgs e) => Log.OpenFolder();

        // ---- updates ----

        static readonly HttpClient http = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };

        void OnCheckUpdate(object sender, RoutedEventArgs e) => CheckUpdate(false);
        void OnDownload(object sender, RoutedEventArgs e) => Process.Start("https://github.com/Cedr1cSun/litemirror/releases/latest");

        // One request for the latest release tag. quiet (at start): only a newer version is announced.
        async void CheckUpdate(bool quiet)
        {
            UpdateButton.IsEnabled = false;
            DownloadButton.Visibility = Visibility.Collapsed;
            UpdateText.Text = quiet ? "" : Strings.Get("Update_Checking");
            try
            {
                var request = new HttpRequestMessage(HttpMethod.Get, "https://api.github.com/repos/Cedr1cSun/litemirror/releases/latest");
                request.Headers.UserAgent.ParseAdd("LiteMirror/" + App.Version);
                HttpResponseMessage response = await http.SendAsync(request);
                response.EnsureSuccessStatusCode();
                Version release = Settings.ReleaseVersion(await response.Content.ReadAsStringAsync());
                if (release == null) throw new FormatException("no tag_name in the answer");
                if (Settings.Newer(release, App.Version))
                {
                    UpdateText.Text = string.Format(Strings.Get("Update_New"), release);
                    DownloadButton.Visibility = Visibility.Visible;
                    if (quiet) Growl.Info(UpdateText.Text);
                }
                else UpdateText.Text = Strings.Get("Update_Latest");
            }
            catch (Exception ex)
            {
                Log.Write(4, "update check: " + ex.Message);
                if (!quiet) UpdateText.Text = string.Format(Strings.Get("Update_Failed"), ex.Message);
            }
            UpdateButton.IsEnabled = true;
        }

        // ---- window = phone ----

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
                dragAspect = shaped && Video.ActualHeight > 0 ? Video.ActualWidth / Video.ActualHeight : 0;
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

        // ---- full screen ----

        void ToggleFullScreen()
        {
            fullScreen = !fullScreen;
            if (fullScreen)
            {
                savedState = WindowState;
                savedBounds = new Rect(Left, Top, Width, Height);
                WindowState = WindowState.Normal;
                ShowNonClientArea = false;
                ResizeMode = ResizeMode.NoResize;
                Sidebar.Visibility = Visibility.Collapsed;
                Pages.Margin = new Thickness(0);
                MirrorPage.CornerRadius = new CornerRadius(0);
                MirrorPage.BorderThickness = new Thickness(0);
                // the chosen monitor, or the one the window is on, in device-independent units
                Forms.Screen[] all = Forms.Screen.AllScreens;
                Forms.Screen target = settings.FullscreenMonitor >= 0 && settings.FullscreenMonitor < all.Length
                    ? all[settings.FullscreenMonitor] : Forms.Screen.FromHandle(new WindowInteropHelper(this).Handle);
                var toDip = PresentationSource.FromVisual(this).CompositionTarget.TransformFromDevice;
                Point origin = toDip.Transform(new Point(target.Bounds.Left, target.Bounds.Top));
                Point extent = toDip.Transform(new Point(target.Bounds.Width, target.Bounds.Height));
                Left = origin.X;
                Top = origin.Y;
                Width = extent.X;
                Height = extent.Y;
            }
            else
            {
                ShowNonClientArea = true;
                ResizeMode = ResizeMode.CanResize;
                Sidebar.Visibility = Visibility.Visible;
                Pages.Margin = new Thickness(12, 0, 12, 12);
                MirrorPage.CornerRadius = new CornerRadius(8);
                MirrorPage.BorderThickness = new Thickness(1);
                Left = savedBounds.Left;
                Top = savedBounds.Top;
                Width = savedBounds.Width;
                Height = savedBounds.Height;
                WindowState = savedState;
                Rail(deviceName != null);
            }
            UpdateOsd();
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
                case Native.Error.Address:         return Strings.Get("ErrAddress");
                default: return string.Format(Strings.Get("ErrOther"), Native.Utf8(Native.lm_error_string(err)));
            }
        }
    }
}
