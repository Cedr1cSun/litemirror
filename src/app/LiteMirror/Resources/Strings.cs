// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Access to Strings.resx / Strings.zh-CN.resx. Written by hand because
// `dotnet build` does not run Visual Studio's resx designer. The culture
// comes from CultureInfo.CurrentUICulture: Windows' display language, or
// the one chosen in Settings.
using System;
using System.Globalization;
using System.Resources;
using System.Threading;
using System.Windows.Markup;

namespace LiteMirror
{
    internal static class Strings
    {
        static readonly ResourceManager Resources = new ResourceManager("LiteMirror.Resources.Strings", typeof(Strings).Assembly);

        public static string Get(string key)
        {
            return Resources.GetString(key, CultureInfo.CurrentUICulture) ?? key;
        }

        // "" follows Windows; otherwise a culture name such as "zh-CN"
        public static void Use(string language)
        {
            CultureInfo culture = language.Length == 0 ? CultureInfo.InstalledUICulture : new CultureInfo(language);
            CultureInfo.DefaultThreadCurrentUICulture = culture;
            Thread.CurrentThread.CurrentUICulture = culture;
        }
    }

    // {local:S Key} in XAML: the string for the current language at load time
    [MarkupExtensionReturnType(typeof(string))]
    public class S : MarkupExtension
    {
        public S(string key) { Key = key; }
        public string Key { get; set; }
        public override object ProvideValue(IServiceProvider serviceProvider) => Strings.Get(Key);
    }
}
