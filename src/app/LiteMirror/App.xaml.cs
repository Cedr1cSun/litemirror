// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System.Windows;

namespace LiteMirror
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            Wpf.Ui.Appearance.ApplicationThemeManager.ApplySystemTheme();
        }
    }
}
