// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
using System;
using System.IO;
using System.Text;
using System.Windows;
using Microsoft.Win32;

namespace LiteMirror
{
    // What the user can change. Kept as key=value lines under %AppData%\LiteMirror;
    // "run at startup" lives only in the registry Run key, which is what Windows reads.
    public class Settings
    {
        const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        const string RunValue = "LiteMirror";

        public string Name = "";        // "" until the first run picks a default in the UI language
        public bool AlwaysOnTop;
        public string Language = "";    // "" = same as Windows, else "en" or "zh-CN"

        static string File_ => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "LiteMirror", "settings.txt");

        public static Settings Load()
        {
            var s = new Settings();
            try
            {
                foreach (string line in File.ReadAllLines(File_, Encoding.UTF8))
                {
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    string key = line.Substring(0, eq), value = line.Substring(eq + 1);
                    if (key == "Name") s.Name = value;
                    if (key == "AlwaysOnTop") s.AlwaysOnTop = value == "1";
                    if (key == "Language") s.Language = value;
                }
            }
            catch (IOException) { }   // first run
            return s;
        }

        public void Save()
        {
            Directory.CreateDirectory(Path.GetDirectoryName(File_));
            File.WriteAllLines(File_, new[] { "Name=" + Name, "AlwaysOnTop=" + (AlwaysOnTop ? "1" : "0"), "Language=" + Language }, Encoding.UTF8);
        }

        public Settings Copy()
        {
            return new Settings { Name = Name, AlwaysOnTop = AlwaysOnTop, Language = Language };
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
                    if (value) key.SetValue(RunValue, "\"" + System.Reflection.Assembly.GetExecutingAssembly().Location + "\" --tray");
                    else key.DeleteValue(RunValue, false);
                }
            }
        }
    }

    public partial class SettingsWindow
    {
        static readonly string[] Languages = { "", "en", "zh-CN" };
        public readonly Settings Result;

        public SettingsWindow(Settings current)
        {
            InitializeComponent();
            Result = current.Copy();
            Title = TitleBar.Title = Strings.Get("Settings");
            NameLabel.Text = Strings.Get("ReceiverNameLabel");
            StartupSwitch.Content = Strings.Get("RunAtStartup");
            TopmostSwitch.Content = Strings.Get("AlwaysOnTop");
            LanguageLabel.Text = Strings.Get("Language");
            CancelButton.Content = Strings.Get("Cancel");
            SaveButton.Content = Strings.Get("Save");
            // each language is named in itself, so anyone can find their own
            LanguageBox.Items.Add(Strings.Get("LanguageAuto"));
            LanguageBox.Items.Add("English");
            LanguageBox.Items.Add("简体中文");
            LanguageBox.SelectedIndex = Math.Max(0, Array.IndexOf(Languages, current.Language));

            NameBox.Text = current.Name;
            TopmostSwitch.IsChecked = current.AlwaysOnTop;
            StartupSwitch.IsChecked = Settings.RunAtStartup;
        }

        void OnSave(object sender, RoutedEventArgs e)
        {
            string name = NameBox.Text.Trim();
            if (name.Length == 0 || Encoding.UTF8.GetByteCount(name) >= 128)   // LM_NAME_MAX
            {
                NameBox.Focus();
                return;
            }
            Result.Name = name;
            Result.AlwaysOnTop = TopmostSwitch.IsChecked == true;
            Result.Language = Languages[Math.Max(0, LanguageBox.SelectedIndex)];
            Result.Save();
            try { Settings.RunAtStartup = StartupSwitch.IsChecked == true; }
            catch (Exception) { }   // a locked-down account: the other settings still apply
            DialogResult = true;
        }

        void OnCancel(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }
    }
}
