// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// "Another phone wants to connect": replace the current one or refuse.
// The phone is kept waiting while this is open, so it refuses by itself
// after a short countdown.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace LiteMirror
{
    public class SecondDeviceDialog : Window
    {
        const int Seconds = 15;

        readonly TextBlock countdown = new TextBlock { Margin = new Thickness(0, 14, 0, 0), FontSize = 12 };
        readonly DispatcherTimer timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        int left = Seconds;

        // DialogResult: true = replace, false = refuse (also on timeout or close)
        public SecondDeviceDialog(Window owner, string newcomer, string current)
        {
            Owner = owner;
            Title = Strings.Get("Ask_Title");
            Style = (Style)Application.Current.FindResource(typeof(Window));
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            ResizeMode = ResizeMode.NoResize;
            SizeToContent = SizeToContent.WidthAndHeight;
            ShowInTaskbar = false;
            FontFamily = owner.FontFamily;
            FontSize = owner.FontSize;
            countdown.SetResourceReference(TextBlock.ForegroundProperty, "SecondaryTextBrush");

            var replace = new Button { Content = Strings.Get("Ask_Replace"), MinWidth = 110, IsDefault = false };
            var refuse = new Button { Content = Strings.Get("Ask_Refuse"), MinWidth = 110, Margin = new Thickness(10, 0, 0, 0), IsCancel = true, IsDefault = true };
            replace.Style = (Style)Application.Current.FindResource("ButtonPrimary");
            System.Windows.Automation.AutomationProperties.SetAutomationId(replace, "AskReplace");
            System.Windows.Automation.AutomationProperties.SetAutomationId(refuse, "AskRefuse");
            replace.Click += (s, e) => { DialogResult = true; };
            refuse.Click += (s, e) => { DialogResult = false; };
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 20, 0, 0) };
            buttons.Children.Add(replace);
            buttons.Children.Add(refuse);

            var body = new StackPanel { Margin = new Thickness(24, 20, 24, 20), MaxWidth = 420 };
            body.Children.Add(new TextBlock { Text = string.Format(Strings.Get("Ask_Body"), newcomer, current), TextWrapping = TextWrapping.Wrap });
            body.Children.Add(countdown);
            body.Children.Add(buttons);
            Content = body;

            Tick(null, null);
            timer.Tick += Tick;
            timer.Start();
            Closed += (s, e) => timer.Stop();
        }

        void Tick(object sender, EventArgs e)
        {
            countdown.Text = string.Format(Strings.Get("Ask_Countdown"), left);
            if (left-- <= 0) DialogResult = false;
        }
    }
}
