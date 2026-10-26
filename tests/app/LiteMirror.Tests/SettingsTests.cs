// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.Linq;
using Xunit;

namespace LiteMirror.Tests
{
    public class SettingsTests
    {
        [Fact]
        public void defaults_are_what_the_design_decided()
        {
            var s = new Settings();
            Assert.Equal("", s.Name);              // the computer name is filled in at run time
            Assert.False(s.AutoStart);
            Assert.Equal(0, s.PinMode);
            Assert.Equal(2, s.SecondDevice);          // ask each time
            Assert.Equal(1080, s.Quality);
            Assert.Equal(60, s.Fps);
            Assert.True(s.HwDecode);
            Assert.True(s.LowLatency);
            Assert.True(s.FollowRotation);
            Assert.Equal(0, s.Scaler);             // the video processor's own filter
            Assert.True(s.AudioEnabled);
            Assert.True(s.FollowVolume);
            Assert.False(s.CloseToTray);
            Assert.Equal(0, s.Theme);
            Assert.Equal("blue", s.ThemeColour);
            Assert.Equal(4, s.LogLevel);           // warnings and errors
            Assert.False(s.CheckUpdates);          // no request to GitHub unless asked
        }

        [Fact]
        public void the_release_version_comes_out_of_githubs_json()
        {
            Assert.Equal(new Version(0, 0, 2), Settings.ReleaseVersion("{\"url\":\"x\",\"tag_name\": \"v0.0.2\",\"name\":\"LiteMirror 0.0.2\"}"));
            Assert.Equal(new Version(1, 4), Settings.ReleaseVersion("{\"tag_name\":\"1.4\"}"));
            Assert.Null(Settings.ReleaseVersion("{\"message\":\"Not Found\"}"));
            Assert.Null(Settings.ReleaseVersion(""));
        }

        [Fact]
        public void a_release_is_newer_only_when_its_number_is_higher()
        {
            Assert.True(Settings.Newer(new Version(0, 0, 2), "0.0.1"));
            Assert.False(Settings.Newer(new Version(0, 0, 1), "0.0.1"));
            Assert.False(Settings.Newer(new Version(1, 2), "1.2.0"));
            Assert.True(Settings.Newer(new Version(1, 2, 1), "1.2"));
            Assert.False(Settings.Newer(new Version(0, 9, 9), "1.0.0"));
        }

        [Fact]
        public void settings_survive_a_round_trip_through_the_file_lines()
        {
            var s = new Settings
            {
                Name = "Study PC", AutoStart = true, PinMode = 2, Pin = "2580", SecondDevice = 1,
                Address = "192.168.1.5", RaopPort = 7000, Quality = 720, Fps = 30, HwDecode = false,
                Brightness = -25, Hue = 100, AudioDevice = "{0.0.0.00000000}.{abc}", Language = "zh-CN",
                Theme = 2, ThemeColour = "teal", CloseToTray = true, LogLevel = 7, AlwaysOnTop = true
            };
            var back = Settings.Parse(s.Lines());
            foreach (var field in typeof(Settings).GetFields())
            {
                Assert.True(Equals(field.GetValue(s), field.GetValue(back)), field.Name + " did not survive");
            }
        }

        [Fact]
        public void unknown_and_broken_lines_are_ignored()
        {
            var s = Settings.Parse(new[] { "Nonsense=1", "Fps=abc", "=3", "no equals sign", "Quality=720", "" });
            Assert.Equal(720, s.Quality);
            Assert.Equal(60, s.Fps);
        }

        [Fact]
        public void a_value_may_contain_an_equals_sign()
        {
            var s = Settings.Parse(new[] { "Name=a=b" });
            Assert.Equal("a=b", s.Name);
            Assert.Contains("Name=a=b", s.Lines());
        }

        [Fact]
        public void lines_cover_every_field_once()
        {
            string[] lines = new Settings().Lines();
            Assert.Equal(typeof(Settings).GetFields().Length, lines.Length);
            Assert.Equal(lines.Length, lines.Select(l => l.Split('=')[0]).Distinct().Count());
        }
    }
}
