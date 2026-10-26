// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.Windows;
using System.Windows.Media;
using HandyControl.Data;
using HandyControl.Themes;
using HandyControl.Tools;
using Microsoft.Win32;

namespace LiteMirror
{
    public partial class App : Application
    {
        Settings settings;

        public static string Version => typeof(App).Assembly.GetName().Version.ToString(3);

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            settings = Settings.Load();
            // earlier builds filled in "<product> (<computer>)" as the name; the default is now the computer name alone
            if (settings.Name == "LiteMirror (" + Environment.MachineName + ")" || settings.Name == "轻投屏 (" + Environment.MachineName + ")")
            {
                settings.Name = "";
            }
            Log.Level = settings.LogLevel;
            Log.Info("LiteMirror " + Version + " starting");
            Strings.Use(settings.Language);
            ApplyTheme(settings);
            // "same as Windows": follow when the user flips Windows between light and dark
            SystemEvents.UserPreferenceChanged += (s, a) =>
            {
                if (a.Category == UserPreferenceCategory.General && settings.Theme == 0) Dispatcher.BeginInvoke(new Action(() => ApplyTheme(settings)));
            };

            // --tray (the Run key uses it): sit in the tray without opening the window
            bool toTray = Array.IndexOf(e.Args, "--tray") >= 0;
            MainWindow = new MainWindow(settings);
            if (toTray)
            {
                // a minimised, hidden show still creates the video window the receiver draws into
                MainWindow.WindowState = WindowState.Minimized;
                MainWindow.ShowInTaskbar = false;
                MainWindow.Show();
                MainWindow.Hide();
                MainWindow.ShowInTaskbar = true;
                MainWindow.WindowState = WindowState.Normal;
            }
            else
            {
                MainWindow.Show();
            }
        }

        protected override void OnExit(ExitEventArgs e)
        {
            Log.Info("exit");
            Log.Close();
            base.OnExit(e);
        }

        // Light or dark and the accent colour, from the settings or from Windows.
        // HandyControl's brushes take their colours when their dictionary is
        // created, so both the skin and the theme dictionary are built afresh,
        // the way HandyControl's own demo switches skins.
        public static void ApplyTheme(Settings s)
        {
            bool dark = s.Theme == 2 || (s.Theme == 0 && WindowsIsDark());
            Color accent = Accent(s.ThemeColour, dark);
            ResourceDictionary skin = ResourceHelper.GetSkin(dark ? SkinType.Dark : SkinType.Default);
            foreach (string key in new[] { "PrimaryColor", "DarkPrimaryColor", "TitleColor", "SecondaryTitleColor" })
            {
                skin[key] = accent;
            }
            // the tint behind the chosen sidebar entry: a wash of the accent over the page
            Color page = dark ? Color.FromRgb(0x1c, 0x1c, 0x1c) : Colors.White;
            float wash = dark ? 0.30f : 0.12f;
            skin["LightPrimaryColor"] = Color.FromRgb(
                (byte)(accent.R * wash + page.R * (1 - wash)),
                (byte)(accent.G * wash + page.G * (1 - wash)),
                (byte)(accent.B * wash + page.B * (1 - wash)));
            SharedResourceDictionary.SharedDictionaries.Clear();
            var merged = Current.Resources.MergedDictionaries;
            merged[0] = skin;
            merged[1] = new ResourceDictionary { Source = new Uri("pack://application:,,,/HandyControl;component/Themes/Theme.xaml") };
            Current.MainWindow?.OnApplyTemplate();
        }

        static bool WindowsIsDark()
        {
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
            {
                return key != null && key.GetValue("AppsUseLightTheme") is int light && light == 0;
            }
        }

        public static readonly string[] Colours = { "blue", "teal", "violet", "amber", "rose", "green" };

        // The accent in each theme: a touch lighter on dark backgrounds so text on it stays readable
        public static Color Accent(string name, bool dark)
        {
            string hex;
            switch (name)
            {
                case "teal":   hex = dark ? "#2CC4B1" : "#0E9F8E"; break;
                case "violet": hex = dark ? "#9B86FF" : "#7C5CFF"; break;
                case "amber":  hex = dark ? "#F2B33D" : "#D98E04"; break;
                case "rose":   hex = dark ? "#FF6B9A" : "#E0457B"; break;
                case "green":  hex = dark ? "#4CC06E" : "#2E9E4F"; break;
                default:       hex = dark ? "#4D8DFF" : "#2F7CF6"; break;
            }
            return (Color)ColorConverter.ConvertFromString(hex);
        }
    }
}
