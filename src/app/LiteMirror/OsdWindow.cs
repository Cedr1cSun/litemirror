// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// The picture information overlay. WPF cannot paint over the Win32 video
// window, so the text lives in a small click-through window of its own
// that is kept at the top-left corner of the picture.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;

namespace LiteMirror
{
    public class OsdWindow : Window
    {
        readonly TextBlock text = new TextBlock
        {
            Foreground = Brushes.White,
            FontFamily = new FontFamily("Consolas, Microsoft YaHei UI"),
            FontSize = 12
        };

        public OsdWindow(Window owner)
        {
            Owner = owner;   // stays above the main window, below other applications
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            AllowsTransparency = true;
            Background = Brushes.Transparent;
            ShowInTaskbar = false;
            ShowActivated = false;
            SizeToContent = SizeToContent.WidthAndHeight;
            Content = new Border
            {
                Background = new SolidColorBrush(Color.FromArgb(150, 0, 0, 0)),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(10, 5, 10, 5),
                Child = text
            };
            SourceInitialized += (s, e) =>
            {
                // clicks go through to the picture; the overlay never takes the focus
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                int style = Native.GetWindowLong(hwnd, Native.GWL_EXSTYLE);
                Native.SetWindowLong(hwnd, Native.GWL_EXSTYLE, style | Native.WS_EX_TRANSPARENT | Native.WS_EX_NOACTIVATE | Native.WS_EX_TOOLWINDOW);
            };
        }

        public string Text
        {
            get => text.Text;
            set => text.Text = value;
        }

        // Put the overlay just inside the top-left corner of `area` (a WPF element of the owner)
        public void Follow(FrameworkElement area)
        {
            if (!area.IsVisible || PresentationSource.FromVisual(area) == null) return;
            Point origin = area.PointToScreen(new Point(0, 0));
            Point dip = PresentationSource.FromVisual(area).CompositionTarget.TransformFromDevice.Transform(origin);
            Left = dip.X + 12;
            Top = dip.Y + 12;
        }
    }
}
