// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.Windows;

namespace LiteMirror
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            Wpf.Ui.Appearance.ApplicationThemeManager.ApplySystemTheme();
            Settings settings = Settings.Load();
            Strings.Use(settings.Language);
            // --tray (the Run key uses it): start receiving in the tray without opening the window
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
    }
}
