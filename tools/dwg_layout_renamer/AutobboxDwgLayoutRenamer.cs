using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Text;

namespace Autobbox.DwgLayoutRenamer
{
    public static class Program
    {
        private const string DrawingDtlFontFile = "ChangFangSong.ttf";
        private const int SEM_FAILCRITICALERRORS = 0x0001;
        private const int SEM_NOGPFAULTERRORBOX = 0x0002;
        private const int SEM_NOOPENFILEERRORBOX = 0x8000;

        [DllImport("kernel32.dll")]
        private static extern uint SetErrorMode(uint mode);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool SetDllDirectory(string lpPathName);

        public static int Main(string[] args)
        {
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
            try
            {
                return Run(args);
            }
            catch (System.Exception ex)
            {
                try { Console.Error.WriteLine("DWG postprocess failed: " + ex.GetType().FullName + " " + ex.Message); } catch { }
                return 1;
            }
        }

        private static int Run(string[] args)
        {
            if (args.Length < 2 || args.Length > 3)
            {
                Console.Error.WriteLine("usage: AutobboxDwgLayoutRenamer_v2.exe <dwg-path> <utf8-layout-name-lines> [font-file]");
                return 64;
            }

            string realDwgDir = @"D:\Program Files\PTC\Creo 10.0.8.0\Common Files\applications\REALDWG";
            string acdbmgdPath = Path.Combine(realDwgDir, "acdbmgd.dll");
            if (!File.Exists(acdbmgdPath))
            {
                Console.Error.WriteLine("acdbmgd.dll not found: " + acdbmgdPath);
                return 66;
            }

            string dwgPath = Path.GetFullPath(args[0]);
            string namesPath = Path.GetFullPath(args[1]);
            string fontFile = args.Length >= 3 ? NormalizeName(args[2]) : DrawingDtlFontFile;
            if (!File.Exists(dwgPath) || !File.Exists(namesPath))
            {
                Console.Error.WriteLine("dwg or names file not found");
                return 66;
            }

            List<string> targetNames = File.ReadAllLines(namesPath, new UTF8Encoding(false))
                .Select(NormalizeName)
                .Where(s => !string.IsNullOrWhiteSpace(s))
                .ToList();
            if (targetNames.Count == 0)
            {
                Console.Error.WriteLine("no layout names supplied");
                return 65;
            }
            if (targetNames.Count != targetNames.Distinct(StringComparer.OrdinalIgnoreCase).Count())
            {
                Console.Error.WriteLine("duplicate layout names are not allowed");
                return 65;
            }

            SetDllDirectory(realDwgDir);
            Environment.SetEnvironmentVariable("PATH", realDwgDir + ";" + Environment.GetEnvironmentVariable("PATH"));
            Directory.SetCurrentDirectory(realDwgDir);
            Assembly acdb = Assembly.LoadFrom(acdbmgdPath);
            RealDwgApi api = new RealDwgApi(acdb);
            object host = CreateHostApplicationServices(api.HostApplicationServicesType, api.DatabaseType, api.FindFileHintType);

            api.RuntimeSystemType.GetMethod("Initialize", new Type[] { api.HostApplicationServicesType, typeof(int) }).Invoke(null, new object[] { host, 0 });
            try
            {
                using (IDisposable db = (IDisposable)Activator.CreateInstance(api.DatabaseType, new object[] { false, true }))
                {
                    object database = db;
                    api.HostApplicationServicesType.GetProperty("WorkingDatabase").SetValue(null, database, null);
                    api.DatabaseType.GetMethod("ReadDwgFile", new Type[] { typeof(string), typeof(FileShare), typeof(bool), typeof(string) })
                        .Invoke(database, new object[] { dwgPath, FileShare.ReadWrite, false, string.Empty });

                    RenameLayouts(api, database, targetNames);
                    if (!string.IsNullOrWhiteSpace(fontFile))
                    {
                        int changed = ApplyTextStyleFont(api, database, fontFile);
                        Console.WriteLine("fontStyles=" + changed.ToString() + " font=" + fontFile);
                    }
                    object currentVersion = Enum.Parse(api.DwgVersionType, "Current");
                    api.DatabaseType.GetMethod("SaveAs", new Type[] { typeof(string), api.DwgVersionType })
                        .Invoke(database, new object[] { dwgPath, currentVersion });
                }
            }
            finally
            {
                api.RuntimeSystemType.GetMethod("Terminate", Type.EmptyTypes).Invoke(null, null);
            }
            return 0;
        }

        private static void RenameLayouts(RealDwgApi api, object database, List<string> targetNames)
        {
            List<object> layoutIds;
            using (IDisposable tr = StartTransaction(api, database))
            {
                object dictId = api.DatabaseType.GetProperty("LayoutDictionaryId").GetValue(database, null);
                object dict = GetObject(api, tr, dictId, false);
                var layouts = new List<Tuple<int, object, string>>();
                foreach (object entry in (IEnumerable)dict)
                {
                    object value = entry.GetType().GetProperty("Value").GetValue(entry, null);
                    object layout = GetObject(api, tr, value, false);
                    string name = (string)api.LayoutType.GetProperty("LayoutName").GetValue(layout, null);
                    if (string.Equals(name, "Model", StringComparison.OrdinalIgnoreCase)) continue;
                    int tab = (int)api.LayoutType.GetProperty("TabOrder").GetValue(layout, null);
                    layouts.Add(Tuple.Create(tab, value, name));
                }
                layouts.Sort((a, b) => a.Item1.CompareTo(b.Item1));
                if (layouts.Count != targetNames.Count)
                {
                    throw new InvalidOperationException("layout count mismatch: dwg=" + layouts.Count + " names=" + targetNames.Count);
                }
                layoutIds = layouts.Select(x => x.Item2).ToList();
                Commit(api, tr);
            }

            string salt = Guid.NewGuid().ToString("N").Substring(0, 8);
            using (IDisposable tr = StartTransaction(api, database))
            {
                for (int i = 0; i < layoutIds.Count; ++i)
                {
                    object layout = GetObject(api, tr, layoutIds[i], true);
                    api.LayoutType.GetProperty("LayoutName").SetValue(layout, "__AB_TMP_" + salt + "_" + i.ToString("00"), null);
                }
                Commit(api, tr);
            }

            using (IDisposable tr = StartTransaction(api, database))
            {
                for (int i = 0; i < layoutIds.Count; ++i)
                {
                    object layout = GetObject(api, tr, layoutIds[i], true);
                    api.LayoutType.GetProperty("LayoutName").SetValue(layout, targetNames[i], null);
                    Console.WriteLine((i + 1).ToString() + "=" + targetNames[i]);
                }
                Commit(api, tr);
            }
        }

        private static int ApplyTextStyleFont(RealDwgApi api, object database, string fontFile)
        {
            int changed = 0;
            using (IDisposable tr = StartTransaction(api, database))
            {
                object textStyleTableId = api.DatabaseType.GetProperty("TextStyleTableId").GetValue(database, null);
                object table = GetObject(api, tr, textStyleTableId, false);
                foreach (object id in (IEnumerable)table)
                {
                    object style = GetObject(api, tr, id, true);
                    api.TextStyleTableRecordType.GetProperty("IsShapeFile").SetValue(style, false, null);
                    api.TextStyleTableRecordType.GetProperty("FileName").SetValue(style, fontFile, null);
                    api.TextStyleTableRecordType.GetProperty("BigFontFileName").SetValue(style, string.Empty, null);
                    string styleName = (string)api.TextStyleTableRecordType.GetProperty("Name").GetValue(style, null);
                    Console.WriteLine("fontStyle=" + styleName + " file=" + fontFile);
                    ++changed;
                }
                Commit(api, tr);
            }
            return changed;
        }

        private static IDisposable StartTransaction(RealDwgApi api, object database)
        {
            object tm = api.DatabaseType.GetProperty("TransactionManager").GetValue(database, null);
            return (IDisposable)tm.GetType().GetMethod("StartTransaction", Type.EmptyTypes).Invoke(tm, null);
        }

        private static object GetObject(RealDwgApi api, object transaction, object objectId, bool forWrite)
        {
            object mode = Enum.Parse(api.OpenModeType, forWrite ? "ForWrite" : "ForRead");
            return transaction.GetType().GetMethod("GetObject", new Type[] { api.ObjectIdType, api.OpenModeType }).Invoke(transaction, new object[] { objectId, mode });
        }

        private static void Commit(RealDwgApi api, object transaction)
        {
            transaction.GetType().GetMethod("Commit", Type.EmptyTypes).Invoke(transaction, null);
        }

        private static object CreateHostApplicationServices(Type hostBaseType, Type databaseType, Type findFileHintType)
        {
            AssemblyName asmName = new AssemblyName("AutobboxRealDwgDynamicHostAssembly");
            AssemblyBuilder asm = AppDomain.CurrentDomain.DefineDynamicAssembly(asmName, AssemblyBuilderAccess.Run);
            ModuleBuilder mod = asm.DefineDynamicModule("main");
            TypeBuilder tb = mod.DefineType("AutobboxRealDwgDynamicHost", TypeAttributes.Public | TypeAttributes.Sealed, hostBaseType);
            MethodInfo baseFind = hostBaseType.GetMethod("FindFile", new Type[] { typeof(string), databaseType, findFileHintType });
            MethodBuilder mb = tb.DefineMethod("FindFile", MethodAttributes.Public | MethodAttributes.Virtual, typeof(string), new Type[] { typeof(string), databaseType, findFileHintType });
            ILGenerator il = mb.GetILGenerator();
            il.Emit(OpCodes.Ldarg_1);
            il.Emit(OpCodes.Ret);
            tb.DefineMethodOverride(mb, baseFind);
            Type hostType = tb.CreateType();
            return Activator.CreateInstance(hostType);
        }

        private static string NormalizeName(string value)
        {
            if (value == null) return string.Empty;
            return value.Trim().Replace('\r', ' ').Replace('\n', ' ');
        }

        private sealed class RealDwgApi
        {
            public readonly Type RuntimeSystemType;
            public readonly Type HostApplicationServicesType;
            public readonly Type DatabaseType;
            public readonly Type FindFileHintType;
            public readonly Type LayoutType;
            public readonly Type TextStyleTableRecordType;
            public readonly Type DwgVersionType;
            public readonly Type OpenModeType;
            public readonly Type ObjectIdType;

            public RealDwgApi(Assembly acdb)
            {
                RuntimeSystemType = acdb.GetType("Autodesk.AutoCAD.Runtime.RuntimeSystem", true);
                HostApplicationServicesType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.HostApplicationServices", true);
                DatabaseType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.Database", true);
                FindFileHintType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.FindFileHint", true);
                LayoutType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.Layout", true);
                TextStyleTableRecordType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.TextStyleTableRecord", true);
                DwgVersionType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.DwgVersion", true);
                OpenModeType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.OpenMode", true);
                ObjectIdType = acdb.GetType("Autodesk.AutoCAD.DatabaseServices.ObjectId", true);
            }
        }
    }
}
