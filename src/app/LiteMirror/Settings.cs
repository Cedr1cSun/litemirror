// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Everything the user can change, kept as Key=Value lines under
// %AppData%\LiteMirror. Each public field is one line, named after the
// field. "Run at sign-in" lives only in the registry Run key, which is
// what Windows reads.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using Microsoft.Win32;

namespace LiteMirror
{
    public class Settings
    {
        // receiving
        public string Name = "";          // "" = the computer name
        public bool AutoStart;            // start receiving when LiteMirror opens
        public int PinMode;               // lm_pin_mode: 0 off, 1 new code per connection, 2 fixed
        public string Pin = "";
        public int SecondDevice = 2;      // lm_second_device: 0 refuse, 1 replace, 2 ask each time
        public string Address = "";       // IPv4 to listen on, "" = all
        public int RaopPort;              // 0 = automatic
        public int AirplayPort;
        // picture
        public int Quality = 1080;        // height the phone is asked for: 1080, 720 or 540
        public int Fps = 60;
        public bool HwDecode = true;
        public bool LowLatency = true;
        public bool FollowRotation = true;
        public int Scaler;                // 0 video processor, 1 bicubic shader when enlarging
        public int FullscreenMonitor = -1; // -1 = the monitor the window is on
        public int Matrix;                // 0 auto, 1 BT.601, 2 BT.709
        public int Range;                 // 0 auto, 1 limited, 2 full
        public int Brightness;            // -100..100
        public int Contrast;
        public int Saturation;
        public int Hue;
        public bool ShowInfo;             // the picture info overlay
        // sound
        public bool AudioEnabled = true;
        public string AudioDevice = "";   // output device id, "" = default
        public bool FollowVolume = true;
        // system
        public string Language = "";      // "" = same as Windows, else "en" or "zh-CN"
        public int Theme;                 // 0 system, 1 light, 2 dark
        public string ThemeColour = "blue";
        public bool CloseToTray;
        public int LogLevel = 4;          // lm_log_level: 3 errors, 4 warnings, 6 info, 7 debug
        public bool AlwaysOnTop;

        const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        const string RunValue = "LiteMirror";

        public static string Folder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "LiteMirror");
        static string File_ => Path.Combine(Folder, "settings.txt");

        static IEnumerable<FieldInfo> Fields => typeof(Settings).GetFields(BindingFlags.Public | BindingFlags.Instance);

        public static Settings Parse(IEnumerable<string> lines)
        {
            var s = new Settings();
            foreach (string line in lines)
            {
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                FieldInfo field = typeof(Settings).GetField(line.Substring(0, eq), BindingFlags.Public | BindingFlags.Instance);
                string value = line.Substring(eq + 1);
                if (field == null) continue;
                if (field.FieldType == typeof(string)) field.SetValue(s, value);
                else if (field.FieldType == typeof(bool)) field.SetValue(s, value == "1");
                else if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n)) field.SetValue(s, n);
            }
            return s;
        }

        public string[] Lines()
        {
            var lines = new List<string>();
            foreach (FieldInfo field in Fields)
            {
                object v = field.GetValue(this);
                string text = v is bool b ? (b ? "1" : "0") : Convert.ToString(v, CultureInfo.InvariantCulture);
                lines.Add(field.Name + "=" + text);
            }
            return lines.ToArray();
        }

        public static Settings Load()
        {
            try { return Parse(File.ReadAllLines(File_, Encoding.UTF8)); }
            catch (IOException) { return new Settings(); }   // first run
        }

        public void Save()
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllLines(File_, Lines(), Encoding.UTF8);
        }

        public static bool RunAtStartup
        {
            get
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RunKey))
                    return key != null && key.GetValue(RunValue) != null;
            }
            set
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RunKey))
                {
                    if (value) key.SetValue(RunValue, "\"" + Assembly.GetExecutingAssembly().Location + "\" --tray");
                    else key.DeleteValue(RunValue, false);
                }
            }
        }
    }
}
