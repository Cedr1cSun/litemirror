// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// The settings page. Every control writes straight into Settings, saves
// and raises Changed with the field's name; MainWindow decides what has to
// happen (apply at once, or restart the receiver).
using System;
using System.Collections.Generic;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using Forms = System.Windows.Forms;

namespace LiteMirror
{
    public partial class SettingsPage : UserControl
    {
        // one entry of a combo box: what is stored and what is shown
        class Choice
        {
            public readonly string Value;
            readonly string text;
            public Choice(string value, string text) { Value = value; this.text = text; }
            public override string ToString() => text;
        }

        readonly Settings s;
        bool loading;

        public event Action<string> Changed;

        public SettingsPage(Settings settings)
        {
            InitializeComponent();
            s = settings;

            Fill(PinModeBox, "Pin_Off", "Pin_New", "Pin_Fixed");
            Fill(SecondBox, "Second_Refuse", "Second_Replace");
            FillAddresses();
            Fill(QualityBox, "1080p", "720p", "540p");
            Fill(FpsBox, "60 fps", "30 fps");
            Fill(ScalerBox, "Scaler_Std", "Scaler_Sharp");
            FillMonitors();
            Fill(MatrixBox, "Matrix_Auto", "Matrix_601", "Matrix_709");
            Fill(RangeBox, "Range_Auto", "Range_Limited", "Range_Full");
            FillAudioDevices();
            LanguageBox.Items.Add(Strings.Get("Lang_Auto"));
            LanguageBox.Items.Add("English");
            LanguageBox.Items.Add("简体中文");
            Fill(ThemeBox, "Theme_System", "Theme_Light", "Theme_Dark");
            foreach (string colour in App.Colours)
            {
                var swatch = new RadioButton { Style = (Style)FindResource("Swatch"), Tag = new SolidColorBrush(App.Accent(colour, false)), ToolTip = Strings.Get("Colour_" + colour) };
                System.Windows.Automation.AutomationProperties.SetAutomationId(swatch, "Swatch_" + colour);
                swatch.Checked += OnSwatch;
                Swatches.Children.Add(swatch);
            }
            Fill(CloseBox, "Close_Quit", "Close_Tray");
            Fill(LogLevelBox, "Log_Error", "Log_Warn", "Log_Info", "Log_Debug");

            Load();
        }

        static readonly string[] Languages = { "", "en", "zh-CN" };
        static readonly int[] Qualities = { 1080, 720, 540 };
        static readonly int[] Rates = { 60, 30 };
        static readonly int[] LogLevels = { 3, 4, 6, 7 };

        void Fill(ComboBox box, params string[] keys)
        {
            foreach (string key in keys) box.Items.Add(Strings.Get(key));
        }

        // Up adapters with an IPv4 address, the way the user knows them: "Wi-Fi · 192.168.1.5"
        void FillAddresses()
        {
            AddressBox.Items.Add(new Choice("", Strings.Get("Address_All")));
            bool stored = s.Address.Length == 0;
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                foreach (UnicastIPAddressInformation ip in nic.GetIPProperties().UnicastAddresses)
                {
                    if (ip.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                    AddressBox.Items.Add(new Choice(ip.Address.ToString(), nic.Name + " · " + ip.Address));
                    stored |= ip.Address.ToString() == s.Address;
                }
            }
            if (!stored) AddressBox.Items.Add(new Choice(s.Address, s.Address + " (" + Strings.Get("Address_Gone") + ")"));
        }

        void FillMonitors()
        {
            MonitorBox.Items.Add(Strings.Get("Monitor_Same"));
            Forms.Screen[] screens = Forms.Screen.AllScreens;
            for (int i = 0; i < screens.Length; i++)
            {
                MonitorBox.Items.Add(string.Format(Strings.Get("Monitor_N"), i + 1, screens[i].Bounds.Width, screens[i].Bounds.Height));
            }
        }

        void FillAudioDevices()
        {
            AudioDeviceBox.Items.Add(new Choice("", Strings.Get("Audio_Default")));
            int count = Native.lm_audio_devices(null, 0);
            if (count <= 0) return;
            var list = new Native.AudioDevice[count];
            count = Math.Min(count, Native.lm_audio_devices(list, count));
            for (int i = 0; i < count; i++)
            {
                AudioDeviceBox.Items.Add(new Choice(Native.Utf8(list[i].id), Native.Utf8(list[i].name)));
            }
        }

        static void Select(ComboBox box, string value)
        {
            for (int i = 0; i < box.Items.Count; i++)
            {
                if (box.Items[i] is Choice c && c.Value == value) { box.SelectedIndex = i; return; }
            }
            box.SelectedIndex = 0;
        }

        static string Chosen(ComboBox box) => box.SelectedItem is Choice c ? c.Value : "";

        // Controls from the settings. Also after a shortcut changed something.
        public void Load()
        {
            loading = true;
            NameBox.Text = s.Name;
            AutoStartSwitch.IsChecked = s.AutoStart;
            PinModeBox.SelectedIndex = s.PinMode;
            PinBox.Text = s.Pin;
            ShowPinRow();
            SecondBox.SelectedIndex = s.SecondDevice;
            Select(AddressBox, s.Address);
            RaopPortBox.Value = s.RaopPort;
            AirplayPortBox.Value = s.AirplayPort;
            QualityBox.SelectedIndex = Math.Max(0, Array.IndexOf(Qualities, s.Quality));
            FpsBox.SelectedIndex = Math.Max(0, Array.IndexOf(Rates, s.Fps));
            HwSwitch.IsChecked = s.HwDecode;
            LowSwitch.IsChecked = s.LowLatency;
            ScalerBox.SelectedIndex = s.Scaler;
            RotateSwitch.IsChecked = s.FollowRotation;
            InfoSwitch.IsChecked = s.ShowInfo;
            MonitorBox.SelectedIndex = s.FullscreenMonitor + 1 < MonitorBox.Items.Count ? s.FullscreenMonitor + 1 : 0;
            MatrixBox.SelectedIndex = s.Matrix;
            RangeBox.SelectedIndex = s.Range;
            BrightnessSlider.Value = s.Brightness;
            ContrastSlider.Value = s.Contrast;
            SaturationSlider.Value = s.Saturation;
            HueSlider.Value = s.Hue;
            AudioSwitch.IsChecked = s.AudioEnabled;
            Select(AudioDeviceBox, s.AudioDevice);
            VolumeSwitch.IsChecked = s.FollowVolume;
            LanguageBox.SelectedIndex = Math.Max(0, Array.IndexOf(Languages, s.Language));
            ThemeBox.SelectedIndex = s.Theme;
            int colour = Math.Max(0, Array.IndexOf(App.Colours, s.ThemeColour));
            ((RadioButton)Swatches.Children[colour]).IsChecked = true;
            StartupSwitch.IsChecked = Settings.RunAtStartup;
            CloseBox.SelectedIndex = s.CloseToTray ? 1 : 0;
            LogLevelBox.SelectedIndex = Math.Max(0, Array.IndexOf(LogLevels, s.LogLevel));
            loading = false;
        }

        void Set(string field, Action apply)
        {
            if (loading) return;
            apply();
            s.Save();
            Changed?.Invoke(field);
        }

        void ShowPinRow()
        {
            Visibility v = PinModeBox.SelectedIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
            PinRow.Visibility = PinLine.Visibility = v;
        }

        // Enter in a text box applies it, like leaving the box
        void OnTextKey(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter) Keyboard.ClearFocus();
        }

        void OnName(object sender, RoutedEventArgs e) => Set("Name", () => s.Name = NameBox.Text.Trim());
        void OnAutoStart(object sender, RoutedEventArgs e) => Set("AutoStart", () => s.AutoStart = AutoStartSwitch.IsChecked == true);
        void OnPinMode(object sender, SelectionChangedEventArgs e)
        {
            ShowPinRow();
            Set("PinMode", () => s.PinMode = PinModeBox.SelectedIndex);
        }
        void OnPin(object sender, RoutedEventArgs e) => Set("Pin", () => s.Pin = PinBox.Text.Trim());
        void OnSecond(object sender, SelectionChangedEventArgs e) => Set("SecondDevice", () => s.SecondDevice = SecondBox.SelectedIndex);
        void OnAddress(object sender, SelectionChangedEventArgs e) => Set("Address", () => s.Address = Chosen(AddressBox));
        void OnPorts(object sender, RoutedEventArgs e) => Set("Ports", () =>
        {
            s.RaopPort = (int)RaopPortBox.Value;
            s.AirplayPort = (int)AirplayPortBox.Value;
        });
        void OnQuality(object sender, SelectionChangedEventArgs e) => Set("Quality", () => s.Quality = Qualities[Math.Max(0, QualityBox.SelectedIndex)]);
        void OnFps(object sender, SelectionChangedEventArgs e) => Set("Fps", () => s.Fps = Rates[Math.Max(0, FpsBox.SelectedIndex)]);
        void OnHw(object sender, RoutedEventArgs e) => Set("HwDecode", () => s.HwDecode = HwSwitch.IsChecked == true);
        void OnLow(object sender, RoutedEventArgs e) => Set("LowLatency", () => s.LowLatency = LowSwitch.IsChecked == true);
        void OnScaler(object sender, SelectionChangedEventArgs e) => Set("Scaler", () => s.Scaler = Math.Max(0, ScalerBox.SelectedIndex));
        void OnRotate(object sender, RoutedEventArgs e) => Set("FollowRotation", () => s.FollowRotation = RotateSwitch.IsChecked == true);
        void OnInfo(object sender, RoutedEventArgs e) => Set("ShowInfo", () => s.ShowInfo = InfoSwitch.IsChecked == true);
        void OnMonitor(object sender, SelectionChangedEventArgs e) => Set("FullscreenMonitor", () => s.FullscreenMonitor = MonitorBox.SelectedIndex - 1);
        void OnColour(object sender, RoutedEventArgs e) => Set("Colour", () =>
        {
            s.Matrix = Math.Max(0, MatrixBox.SelectedIndex);
            s.Range = Math.Max(0, RangeBox.SelectedIndex);
            s.Brightness = (int)BrightnessSlider.Value;
            s.Contrast = (int)ContrastSlider.Value;
            s.Saturation = (int)SaturationSlider.Value;
            s.Hue = (int)HueSlider.Value;
        });
        void OnResetColour(object sender, RoutedEventArgs e)
        {
            s.Matrix = s.Range = s.Brightness = s.Contrast = s.Saturation = s.Hue = 0;
            Load();
            s.Save();
            Changed?.Invoke("Colour");
        }
        void OnAudio(object sender, RoutedEventArgs e) => Set("AudioEnabled", () => s.AudioEnabled = AudioSwitch.IsChecked == true);
        void OnAudioDevice(object sender, SelectionChangedEventArgs e) => Set("AudioDevice", () => s.AudioDevice = Chosen(AudioDeviceBox));
        void OnVolume(object sender, RoutedEventArgs e) => Set("FollowVolume", () => s.FollowVolume = VolumeSwitch.IsChecked == true);
        void OnLanguage(object sender, SelectionChangedEventArgs e) => Set("Language", () => s.Language = Languages[Math.Max(0, LanguageBox.SelectedIndex)]);
        void OnTheme(object sender, SelectionChangedEventArgs e) => Set("Theme", () => s.Theme = Math.Max(0, ThemeBox.SelectedIndex));
        void OnSwatch(object sender, RoutedEventArgs e) => Set("ThemeColour", () => s.ThemeColour = App.Colours[Swatches.Children.IndexOf((UIElement)sender)]);
        void OnStartup(object sender, RoutedEventArgs e)
        {
            if (loading) return;
            try { Settings.RunAtStartup = StartupSwitch.IsChecked == true; }
            catch (Exception) { StartupSwitch.IsChecked = Settings.RunAtStartup; }   // a locked-down account
        }
        void OnClose(object sender, SelectionChangedEventArgs e) => Set("CloseToTray", () => s.CloseToTray = CloseBox.SelectedIndex == 1);
        void OnLogLevel(object sender, SelectionChangedEventArgs e) => Set("LogLevel", () => s.LogLevel = LogLevels[Math.Max(0, LogLevelBox.SelectedIndex)]);
        void OnOpenLogs(object sender, RoutedEventArgs e) => Log.OpenFolder();
    }
}
