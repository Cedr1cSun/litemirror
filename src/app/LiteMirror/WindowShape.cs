// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// The arithmetic behind "the window is the phone": the user owns the window's
// area, the phone's screen owns its shape. A Rect is a whole window; chrome is
// what the frame adds round the video area. Units only have to agree.
using System;
using System.Windows;

namespace LiteMirror
{
    public static class WindowShape
    {
        // WMSZ_* from WinUser.h: which edge WM_SIZING says is being dragged
        public const int EdgeLeft = 1, EdgeRight = 2, EdgeTop = 3, EdgeTopLeft = 4,
                         EdgeTopRight = 5, EdgeBottom = 6, EdgeBottomLeft = 7, EdgeBottomRight = 8;

        // iOS scales the mirror to the display, so the two orientations are not exact transposes
        const double Tolerance = 0.02;

        static bool Close(double a, double b) => Math.Abs(a - b) <= Tolerance * b;

        // A picture of this size arrived. Null: leave the window alone (the sink
        // letterboxes it); otherwise the window turned to the phone's shape, same area.
        public static Rect? ForPicture(Rect window, Size chrome, Rect work, Size screen, Size picture)
        {
            double aspect = picture.Width / picture.Height, phone = screen.Width / screen.Height;
            if (!Close(aspect, phone) && !Close(aspect, 1 / phone)) return null;
            double areaW = window.Width - chrome.Width, areaH = window.Height - chrome.Height;
            if (Close(aspect, areaW / areaH)) return null;
            double w = Math.Sqrt(areaW * areaH * aspect);
            return Place(window, chrome, work, w, w / aspect);
        }

        // The video area at exactly this size (the picture's own pixels)
        public static Rect Fit(Rect window, Size chrome, Rect work, Size area)
        {
            return Place(window, chrome, work, area.Width, area.Height);
        }

        // Same centre as before, shrunk to the work area if it must be, then pushed inside it
        static Rect Place(Rect window, Size chrome, Rect work, double areaW, double areaH)
        {
            double maxW = work.Width - chrome.Width, maxH = work.Height - chrome.Height;
            if (areaW > maxW) { areaH *= maxW / areaW; areaW = maxW; }
            if (areaH > maxH) { areaW *= maxH / areaH; areaH = maxH; }
            double w = areaW + chrome.Width, h = areaH + chrome.Height;
            double left = window.Left + (window.Width - w) / 2, top = window.Top + (window.Height - h) / 2;
            left = Math.Max(work.Left, Math.Min(left, work.Right - w));
            top = Math.Max(work.Top, Math.Min(top, work.Bottom - h));
            return new Rect(left, top, w, h);
        }

        // The user drags an edge (WM_SIZING) and the video area keeps its aspect: a
        // side or a corner sets the width and the height follows; top or bottom the reverse.
        public static Rect KeepAspect(Rect proposed, int edge, Size chrome, double aspect)
        {
            Rect r = proposed;
            if (edge == EdgeTop || edge == EdgeBottom)
            {
                r.Width = (r.Height - chrome.Height) * aspect + chrome.Width;
            }
            else
            {
                double h = (r.Width - chrome.Width) / aspect + chrome.Height;
                if (edge == EdgeTopLeft || edge == EdgeTopRight) r.Y = r.Bottom - h;
                r.Height = h;
            }
            return r;
        }
    }
}
