// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// One log file a day under %AppData%\LiteMirror\logs, fed by the core's
// on_log and by the app itself. Lines above the chosen level are dropped
// before they reach the disk.
using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace LiteMirror
{
    internal static class Log
    {
        static readonly object gate = new object();
        static StreamWriter writer;
        static string day;

        public static int Level = 4;   // lm_log_level: 3 errors, 4 warnings, 6 info, 7 debug
        public static string Folder => Path.Combine(Settings.Folder, "logs");

        public static void Write(int level, string message)
        {
            if (level > Level) return;
            string now = DateTime.Now.ToString("yyyyMMdd");
            lock (gate)
            {
                try
                {
                    if (writer == null || day != now)
                    {
                        writer?.Dispose();
                        Directory.CreateDirectory(Folder);
                        writer = new StreamWriter(Path.Combine(Folder, "litemirror-" + now + ".log"), true, new UTF8Encoding(false)) { AutoFlush = true };
                        day = now;
                    }
                    writer.WriteLine(DateTime.Now.ToString("HH:mm:ss.fff") + " " + Tag(level) + " " + message);
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }

        public static void Error(string message) => Write(3, message);
        public static void Info(string message) => Write(6, message);

        static string Tag(int level) => level <= 3 ? "E" : level == 4 ? "W" : level <= 6 ? "I" : "D";

        public static void OpenFolder()
        {
            Directory.CreateDirectory(Folder);
            Process.Start("explorer.exe", Folder);
        }

        public static void Close()
        {
            lock (gate)
            {
                writer?.Dispose();
                writer = null;
            }
        }
    }
}
