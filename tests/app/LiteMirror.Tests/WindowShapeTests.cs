// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System.Windows;
using Xunit;

namespace LiteMirror.Tests
{
    public class WindowShapeTests
    {
        // an iPad as iOS reports it to an external display: portrait 754x1080, landscape 1546x1080
        static readonly Size Portrait = new Size(754, 1080);
        static readonly Size Landscape = new Size(1546, 1080);
        static readonly Size Chrome = new Size(0, 40);   // the title bar
        static readonly Rect Work = new Rect(0, 0, 2560, 1400);

        [Fact]
        public void first_picture_turns_the_window_to_the_phone_shape_keeping_area()
        {
            var window = new Rect(100, 300, 1000, 600);   // video area 1000x560, centre (600, 600)
            Rect? r = WindowShape.ForPicture(window, Chrome, Work, Portrait, Portrait);
            Assert.True(r.HasValue);
            double w = r.Value.Width, h = r.Value.Height - 40;
            Assert.Equal(1000 * 560, w * h, 0);
            Assert.Equal(754.0 / 1080, w / h, 3);
            Assert.Equal(600, r.Value.Left + r.Value.Width / 2, 1);   // same centre
            Assert.Equal(600, r.Value.Top + r.Value.Height / 2, 1);
        }

        [Fact]
        public void rotation_is_recognised_within_tolerance()
        {
            // 1080/754 = 1.4324 and 1546/1080 = 1.4315: not an exact transpose
            var window = new Rect(100, 100, 600, 900);
            Assert.True(WindowShape.ForPicture(window, Chrome, Work, Portrait, Landscape).HasValue);
        }

        [Fact]
        public void a_bare_video_is_left_to_the_letterbox()
        {
            var window = new Rect(100, 100, 600, 900);
            Assert.Null(WindowShape.ForPicture(window, Chrome, Work, Portrait, new Size(1920, 1080)));
        }

        [Fact]
        public void the_same_shape_leaves_the_window_alone()
        {
            var window = new Rect(100, 100, 754, 1120);   // already portrait
            Assert.Null(WindowShape.ForPicture(window, Chrome, Work, Portrait, Portrait));
        }

        [Fact]
        public void a_turn_shrinks_to_the_work_area()
        {
            var work = new Rect(0, 0, 1280, 720);
            var window = new Rect(0, 0, 1200, 640);   // area 1200x600: too tall once portrait
            Rect r = WindowShape.ForPicture(window, Chrome, work, Portrait, Portrait).Value;
            Assert.True(work.Contains(r));
            Assert.Equal(754.0 / 1080, r.Width / (r.Height - 40), 3);
        }

        [Fact]
        public void fit_puts_the_picture_at_its_own_size_round_the_same_centre()
        {
            var window = new Rect(200, 400, 1000, 600);   // centre (700, 700)
            Rect r = WindowShape.Fit(window, Chrome, Work, Portrait);
            Assert.Equal(754, r.Width, 1);
            Assert.Equal(1120, r.Height, 1);
            Assert.Equal(700, r.Left + r.Width / 2, 1);
            Assert.Equal(700, r.Top + r.Height / 2, 1);
        }

        [Fact]
        public void picture_in_picture_sits_in_the_bottom_right_corner_at_a_third_of_the_height()
        {
            Rect r = WindowShape.Corner(Work, 16.0 / 9, 1.0 / 3);
            Assert.Equal(1400 / 3.0, r.Height, 3);
            Assert.Equal(1400 / 3.0 * 16 / 9, r.Width, 3);
            Assert.Equal(2560 - 16, r.Right, 3);
            Assert.Equal(1400 - 16, r.Bottom, 3);
        }

        [Fact]
        public void picture_in_picture_of_a_portrait_phone_is_narrow()
        {
            Rect r = WindowShape.Corner(Work, 754.0 / 1080, 1.0 / 3);
            Assert.Equal(754.0 / 1080, r.Width / r.Height, 3);
            Assert.Equal(1400 / 3.0, r.Height, 3);
        }

        [Fact]
        public void picture_in_picture_never_overflows_a_narrow_work_area()
        {
            Rect r = WindowShape.Corner(new Rect(0, 0, 800, 1400), 16.0 / 9, 1.0 / 3);
            Assert.Equal(800 - 32, r.Width, 3);
            Assert.Equal(16, r.Left, 3);
        }

        [Fact]
        public void dragging_the_right_edge_sets_the_height()
        {
            Rect r = WindowShape.KeepAspect(new Rect(0, 0, 1000, 600), WindowShape.EdgeRight, Chrome, 2.0);
            Assert.Equal(new Rect(0, 0, 1000, 540), r);
        }

        [Fact]
        public void dragging_the_top_edge_sets_the_width_and_keeps_the_bottom()
        {
            Rect r = WindowShape.KeepAspect(new Rect(0, 100, 1000, 600), WindowShape.EdgeTop, Chrome, 2.0);
            Assert.Equal(new Rect(0, 100, 1120, 600), r);
        }

        [Fact]
        public void dragging_the_top_left_corner_keeps_the_bottom_right()
        {
            Rect r = WindowShape.KeepAspect(new Rect(0, 0, 1000, 600), WindowShape.EdgeTopLeft, Chrome, 2.0);
            Assert.Equal(new Rect(0, 60, 1000, 540), r);
        }
    }
}
